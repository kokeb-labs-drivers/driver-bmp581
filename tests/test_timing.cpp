/**
 * @file test_timing.cpp
 * @brief The readings the datasheet leaves open, each offered by a model knob: how late
 * OSR_EFF.odr_is_valid and a forced pwr_mode show, whether deep standby takes writes, the FIFO
 * flush time, and what MISO shows in the SPI frame that only selects SPI (sections 4.3, 4.6.4,
 * 5.1, 7.35). The driver works under every reading.
 */
#include <array>
#include <cstddef>
#include <cstdint>

#include "fixture.hpp"
#include "test_harness.hpp"

using bmp581::Config;
using bmp581::Error;
using bmp581::FifoSample;
using bmp581::PowerMode;
using bmp581_test::MinimalPort;
using bmp581_test::MinimalSpiPort;
using bmp581_test::ModelMode;
using bmp581_test::RawC;
using bmp581_test::RawPa;
using bmp581_test::Rig;
namespace chip = bmp581_test::chip;
namespace reg = bmp581::reg;

namespace {

// --- OSR_EFF.odr_is_valid -----------------------------------------------------------------------

template <typename P>
void NormalWithValidLag() {
  for (const uint32_t lag : {0U, 600U, 3000U}) {
    Rig<P> r(bmp581::LowPowerPreset());
    r.Chip().odr_valid_lag_us = lag;
    REQUIRE(r.Start());
    CHECK_EQ(r.Chip().Mode(), ModelMode::Normal);
    CHECK((r.sensor.GetLastOsrEff() & 0x80U) != 0U);
    CHECK(r.sensor.SetPowerMode(PowerMode::Standby).ok());
    CHECK(r.sensor.SetPowerMode(PowerMode::Normal).ok());
  }
  // A data rate the chip refuses is still ConfigRejected, once the deadline has passed.
  Rig<P> r(bmp581::LowPowerPreset());
  r.Chip().reject_odr = true;
  r.Chip().odr_valid_lag_us = 600;
  CHECK_EQ(r.sensor.EnsureInitialized().error, Error::ConfigRejected);
  CHECK_EQ(r.Chip().Mode(), ModelMode::Standby);
}

TEST_CASE("Normal mode waits for odr_is_valid until the deadline, on I2C and SPI") {
  NormalWithValidLag<MinimalPort>();
  NormalWithValidLag<MinimalSpiPort>();
}

// --- Forced pwr_mode ----------------------------------------------------------------------------

TEST_CASE("MeasureOnce() waits for pwr_mode to show forced, at most t_startup") {
  for (const uint32_t lag : {0U, 300U, 1200U}) {
    Rig<MinimalPort> r;
    REQUIRE(r.Start());
    r.Chip().mode_lag_us = lag;
    const auto m = r.sensor.MeasureOnce();
    CHECK(m.ok());
  }
  // A forced write that never shows (a lost write) is WriteMismatch, never the old data.
  Rig<MinimalPort> r;
  REQUIRE(r.Start());
  REQUIRE(r.sensor.MeasureOnce().ok());
  r.Bus().drop_transfer = r.Next() + 2; // INT_STATUS, sentinel, the forced write
  CHECK_EQ(r.sensor.MeasureOnce().error, Error::WriteMismatch);
  CHECK(r.sensor.MeasureOnce().ok());
}

// --- Deep standby -------------------------------------------------------------------------------

template <typename P>
void EveryFlowUnder(bool drops) {
  const std::array<Config, 5> configs{Config{}, bmp581::ForcedPreset(), bmp581::LowPowerPreset(),
                                      bmp581::HighResolutionPreset(), bmp581::FifoPreset()};
  for (const Config& c : configs) {
    Rig<P> r(c);
    r.Chip().deep_standby_drops_writes = drops;
    REQUIRE(r.Start());
    CHECK(r.sensor.IsConfigKnown());
    CHECK_EQ(r.Chip().writes_lost_deep, 0);
    CHECK_EQ(r.Chip().Reg(chip::kOsrConfig), bmp581::detail::OsrConfigValue(c));
    CHECK(r.sensor.SetPowerMode(PowerMode::Standby).ok());
    CHECK(r.sensor.MeasureOnce().ok());
    for (const Config& next : configs) {
      CHECK(r.sensor.ApplyConfig(next).ok());
      CHECK_EQ(r.Chip().Reg(chip::kFifoSel), bmp581::detail::FifoSelValue(next));
      CHECK_EQ(r.Chip().Reg(chip::kDspIir), bmp581::detail::DspIirValue(next));
    }
    CHECK_EQ(r.Chip().writes_lost_deep, 0);
  }
}

TEST_CASE("No write is lost in deep standby: standby with deep_dis = 1 first (both readings)") {
  EveryFlowUnder<MinimalPort>(true);
  EveryFlowUnder<MinimalPort>(false);
  EveryFlowUnder<MinimalSpiPort>(true);
  EveryFlowUnder<MinimalSpiPort>(false);
}

TEST_CASE("The start-up leaves deep standby before the first configuration write") {
  Rig<MinimalPort> r;
  REQUIRE(r.Start());
  // Before the soft reset and after the reset checks: ODR_CONFIG standby with deep_dis = 1; after
  // the second, the marker.
  REQUIRE(r.Bus().log.size() > 2U);
  CHECK(r.Bus().log[0].write && (r.Bus().log[0].reg == chip::kOdrConfig));
  CHECK_EQ(r.Bus().log[1].reg, chip::kCmd);
  std::size_t odr = r.Bus().log.size();
  for (std::size_t i = 2; i < r.Bus().log.size(); ++i) {
    if (r.Bus().log[i].write && (r.Bus().log[i].reg == chip::kOdrConfig)) {
      odr = i;
      break;
    }
  }
  REQUIRE(odr + 2U < r.Bus().log.size());
  REQUIRE(!r.Bus().log[odr].bytes.empty());
  CHECK_EQ(r.Bus().log[odr].bytes[0] & 0x83U, 0x80U);
  REQUIRE(odr + 3U < r.Bus().log.size());
  CHECK(!r.Bus().log[odr + 1U].write); // NVM_DATA read: 0x00 0x00 after the reset
  CHECK_EQ(r.Bus().log[odr + 1U].reg, chip::kNvmDataLsb);
  CHECK_EQ(r.Bus().log[odr + 2U].reg, chip::kNvmDataLsb);
  CHECK_EQ(r.Bus().log[odr + 3U].reg, chip::kNvmDataMsb);
  CHECK_EQ(r.Chip().Reg(chip::kNvmDataLsb), bmp581::detail::kMarker[0]);
  CHECK_EQ(r.Chip().Reg(chip::kNvmDataMsb), bmp581::detail::kMarker[1]);
  // The configured deep standby is written last.
  std::size_t last_write = 0;
  for (std::size_t i = 0; i < r.Bus().log.size(); ++i) {
    if (r.Bus().log[i].write) {
      last_write = i;
    }
  }
  CHECK_EQ(r.Bus().log[last_write].reg, chip::kOdrConfig);
  CHECK_EQ(r.Chip().Reg(chip::kOdrConfig) & chip::kDeepDis, 0);
}

// --- FIFO flush ---------------------------------------------------------------------------------

TEST_CASE("With the FIFO on, a setter returns after the flush time t_reconf (section 4.6.4)") {
  Rig<MinimalPort> on(bmp581::FifoPreset());
  REQUIRE(on.Start());
  Config off_config = bmp581::FifoPreset();
  off_config.fifo_frame = bmp581::FifoFrame::Disabled;
  Rig<MinimalPort> off(off_config);
  REQUIRE(off.Start());
  const uint64_t on_start = on.Chip().now_us;
  const uint64_t off_start = off.Chip().now_us;
  REQUIRE(on.sensor.SetPowerMode(PowerMode::Standby).ok());
  REQUIRE(off.sensor.SetPowerMode(PowerMode::Standby).ok());
  CHECK((on.Chip().now_us - on_start) >= ((off.Chip().now_us - off_start) + bmp581::kReconfUs));
  const uint64_t applied = on.Chip().now_us;
  REQUIRE(on.sensor.ApplyConfig(bmp581::FifoPreset()).ok());
  CHECK((on.Chip().now_us - applied) >= bmp581::kReconfUs);
  const uint64_t normal = on.Chip().now_us;
  REQUIRE(on.sensor.SetPowerMode(PowerMode::Continuous).ok());
  CHECK((on.Chip().now_us - normal) >= bmp581::kReconfUs);
}

// --- The SPI selection frame --------------------------------------------------------------------

/// A started SPI driver whose chip reset; the next frame only selects SPI and shows `fill`.
void ResetWithFill(Rig<MinimalSpiPort>& r, uint8_t fill) {
  r.Chip().select_fill = fill;
  r.Chip().PowerOnReset();
  r.Chip().Elapse(5000);
}

TEST_CASE("On SPI no selection frame passes for data, whatever MISO shows (all 256 fills)") {
  for (uint32_t f = 0U; f < 256U; ++f) {
    const auto fill = static_cast<uint8_t>(f);
    {
      Rig<MinimalSpiPort> r;
      REQUIRE(r.Start());
      ResetWithFill(r, fill);
      CHECK_EQ(r.sensor.CheckForReset().error, Error::DeviceReset);
    }
    {
      Rig<MinimalSpiPort> r;
      REQUIRE(r.Start());
      ResetWithFill(r, fill);
      CHECK_EQ(r.sensor.ReadInterruptStatus().error, Error::DeviceReset);
    }
    {
      Rig<MinimalSpiPort> r(bmp581::LowPowerPreset());
      REQUIRE(r.Start());
      ResetWithFill(r, fill);
      CHECK_EQ(r.sensor.WaitDataReady().error, Error::DeviceReset);
    }
    {
      Rig<MinimalSpiPort> r(bmp581::FifoPreset());
      REQUIRE(r.Start());
      ResetWithFill(r, fill);
      CHECK_EQ(r.sensor.ReadFifoCount().error, Error::DeviceReset);
    }
    {
      Rig<MinimalSpiPort> r;
      REQUIRE(r.Start());
      ResetWithFill(r, fill);
      CHECK_EQ(r.sensor.ReadRegister(reg::kStatus).error, Error::DeviceReset);
    }
    {
      Rig<MinimalSpiPort> r;
      REQUIRE(r.Start());
      ResetWithFill(r, fill);
      CHECK(!r.sensor.EnsureInitialized().ok() || (r.Chip().soft_resets == 2));
    }
  }
}

TEST_CASE("On SPI a reset between FIFO_COUNT and FIFO_DATA is found before samples return") {
  for (uint32_t f = 0U; f < 256U; f += 5U) {
    Rig<MinimalSpiPort> r(bmp581::FifoPreset());
    REQUIRE(r.Start());
    r.Chip().select_fill = static_cast<uint8_t>(f);
    r.Chip().reset_busy_us = 0; // the chip answers the very next frame
    r.Chip().Elapse(250'000);
    r.Bus().reset_before = r.Next() + 1; // FIFO_COUNT, then the FIFO_DATA read
    std::array<FifoSample, 8> out{};
    CHECK_EQ(r.sensor.ReadFifo(out).error, Error::DeviceReset);
  }
}

TEST_CASE("A FIFO frame out of range runs the reset check before the samples return") {
  Rig<MinimalPort> r(bmp581::FifoPreset());
  REQUIRE(r.Start());
  r.Chip().temperature_raw = RawC(90);
  r.Chip().Elapse(250'000);
  const int checks = r.Bus().ReadsFrom(chip::kOsrConfig);
  std::array<FifoSample, 4> out{};
  REQUIRE(r.sensor.ReadFifo(out).ok());
  CHECK_EQ(out[0].error, Error::OutOfRange);
  CHECK_EQ(r.Bus().ReadsFrom(chip::kOsrConfig) - checks, 1);
}

// --- The marker ---------------------------------------------------------------------------------

TEST_CASE("A reset during ApplyConfig() whose por read is disturbed is still found") {
  // The chip answers at once after the reset, so the sequence's own OSR_CONFIG write covers the
  // sentinel; INT_STATUS reads lose their por. The marker, which the sequence does not write, still
  // shows it; when the marker had to be written again first (a measurement from deep standby since,
  // with an NVM download reaching NVM_DATA or not), the sentinel check after that write does.
  for (int pass = 0; pass < 4; ++pass) {
    for (int k = 0; k < 26; ++k) {
      for (int j = k; j < 28; ++j) {
        Rig<MinimalPort> r;
        r.Chip().reset_busy_us = 0;
        r.Chip().nvm_boot_us = 0;
        r.Chip().nvm_download_rewrites_data = pass > 0;
        r.Chip().nvm_download_data = {static_cast<uint8_t>((pass == 2) ? 0x12 : 0x00),
                                      static_cast<uint8_t>((pass == 2) ? 0x34 : 0x00)};
        REQUIRE(r.Start());
        if (pass != 3) {
          REQUIRE(r.sensor.MeasureOnce().ok()); // from deep standby: the marker is no longer fresh
        }
        const int first = r.Next();
        r.Bus().reset_before = first + k;
        r.Bus().corrupt_read = first + j;
        r.Bus().corrupt_mask = chip::kPor;
        const auto result = r.sensor.ApplyConfig(bmp581::HighResolutionPreset());
        if (r.Chip().resets < 2) {
          continue; // the call ended before transfer k
        }
        CHECK(!result.ok());
        if (r.sensor.IsInitialized()) {
          CHECK(!r.sensor.CheckForReset().ok());
        }
      }
    }
  }
}

/// A started driver whose chip measured from deep standby with the NVM download reaching NVM_DATA:
/// the marker is gone, and no reset happened.
/// What the NVM download may leave in NVM_DATA: the reset value (an unprogrammed word), another
/// word, or the marker itself.
constexpr std::array<std::array<uint8_t, 2>, 3> kDownloads{
    {{0x00, 0x00}, {0x12, 0x34}, {bmp581::detail::kMarker[0], bmp581::detail::kMarker[1]}}};

template <typename P>
void MarkerOverwritten(Rig<P>& r, std::array<uint8_t, 2> data) {
  r.Chip().nvm_download_rewrites_data = true;
  r.Chip().nvm_download_data = data;
  REQUIRE(r.Start());
  REQUIRE(r.sensor.MeasureOnce().ok());
  REQUIRE(r.Chip().Reg(chip::kNvmDataLsb) == data[0]);
}

template <typename P>
void NvmDownloadIsNoReset(std::array<uint8_t, 2> data) {
  {
    Rig<P> r;
    MarkerOverwritten(r, data);
    for (int i = 0; i < 3; ++i) {
      CHECK(r.sensor.ApplyConfig(Config{}).ok()); // the marker is written again, no reset
      CHECK(r.sensor.MeasureOnce().ok());
      CHECK(r.sensor.CheckForReset().ok());
    }
    CHECK_EQ(r.Chip().resets, 1);
  }
  {
    // A real power-on reset after the marker was lost is still found.
    Rig<P> r;
    MarkerOverwritten(r, data);
    r.Chip().PowerOnReset();
    r.Chip().Elapse(5000);
    CHECK_EQ(r.sensor.ApplyConfig(Config{}).error, Error::DeviceReset);
    CHECK(r.Start());
  }
  {
    // Normal mode with deep standby between measurements, the low-power preset.
    Rig<P> r(bmp581::LowPowerPreset());
    r.Chip().nvm_download_rewrites_data = true;
    r.Chip().nvm_download_data = data;
    REQUIRE(r.Start());
    r.Chip().Elapse(3'000'000);
    CHECK(r.sensor.ApplyConfig(bmp581::LowPowerPreset()).ok());
    r.Chip().Elapse(3'000'000);
    CHECK(r.sensor.ApplyConfig(bmp581::LowPowerPreset()).ok());
  }
}

TEST_CASE("An NVM download that overwrites the marker is not a reset (both readings)") {
  for (const auto& data : kDownloads) {
    NvmDownloadIsNoReset<MinimalPort>(data);
    NvmDownloadIsNoReset<MinimalSpiPort>(data);
  }
}

TEST_CASE("Only NVM_DATA at its reset value, read twice, is a reset") {
  // A disturbed marker read, before the sequence (index 3) or at the read-back (index 20), is read
  // again and does not count as a reset: INT_STATUS, sentinel, ODR, NVM_DATA, INT_SOURCE,
  // INT_STATUS, 12 writes, A, B, NVM_DATA.
  for (const int at : {3, 20}) {
    Rig<MinimalPort> r;
    REQUIRE(r.Start());
    r.Bus().corrupt_read = r.Next() + at;
    r.Bus().corrupt_byte = 0;
    r.Bus().corrupt_mask = bmp581::detail::kMarker[0];
    CHECK(r.sensor.ApplyConfig(Config{}).ok());
  }
}

template <typename P>
void ResetsWithMarkerLost(std::array<uint8_t, 2> data) {
  // Every call still finds a reset after the marker was lost, and a reset at any transfer of
  // ApplyConfig() is never reported Ok.
  {
    Rig<P> r;
    MarkerOverwritten(r, data);
    r.Chip().PowerOnReset();
    r.Chip().Elapse(5000);
    CHECK_EQ(r.sensor.MeasureOnce().error, Error::DeviceReset);
  }
  {
    Rig<P> r;
    MarkerOverwritten(r, data);
    r.Chip().PowerOnReset();
    r.Chip().Elapse(5000);
    CHECK_EQ(r.sensor.CheckForReset().error, Error::DeviceReset);
  }
  for (int k = 0; k < 40; ++k) {
    Rig<P> r;
    MarkerOverwritten(r, data);
    r.Bus().reset_before = r.Next() + k;
    const auto result = r.sensor.ApplyConfig(bmp581::HighResolutionPreset());
    if (r.Chip().resets < 2) {
      CHECK(result.ok());
      continue;
    }
    CHECK(!result.ok());
    r.Chip().Elapse(5000);
    if (r.sensor.IsInitialized()) {
      CHECK_EQ(r.sensor.CheckForReset().error, Error::DeviceReset);
    }
  }
}

TEST_CASE("Resets are found with the marker lost to an NVM download") {
  for (const auto& data : kDownloads) {
    ResetsWithMarkerLost<MinimalPort>(data);
    ResetsWithMarkerLost<MinimalSpiPort>(data);
  }
}

// --- A second read of the data
// --------------------------------------------------------------------

TEST_CASE("Verify::ReadBack reads the data again: a flipped bit is outvoted or refused") {
  for (int which = 0; which < 3; ++which) {
    // The first data read, the second or the third disturbed in one bit: the exact value.
    Rig<MinimalPort> r;
    REQUIRE(r.Start());
    REQUIRE(r.sensor.MeasureOnce().ok());
    // ReadBack: the 12-byte read, the reset check, then the 6-byte reads.
    r.Bus().corrupt_read = r.Next() + ((which == 0) ? 0 : (which + 1));
    r.Bus().corrupt_byte = 0;
    r.Bus().corrupt_mask = 0x40;
    const auto m = r.sensor.ReadMeasurement(bmp581::Verify::ReadBack);
    REQUIRE(m.ok());
    CHECK_EQ(m.value.temperature_raw, r.Chip().temperature_raw);
    CHECK_EQ(m.value.pressure_raw, r.Chip().pressure_raw);
  }
  {
    // Three reads that all differ: InvalidReply.
    Rig<MinimalPort> r;
    REQUIRE(r.Start());
    REQUIRE(r.sensor.MeasureOnce().ok());
    r.Bus().corrupt_read = r.Next() + 2;
    r.Bus().corrupt_reads = 2;
    r.Bus().corrupt_byte = 0;
    r.Bus().corrupt_mask = 0x01;
    CHECK_EQ(r.sensor.ReadMeasurement(bmp581::Verify::ReadBack).error, Error::InvalidReply);
    CHECK(r.sensor.ReadMeasurement(bmp581::Verify::ReadBack).ok());
  }
  {
    // A line stuck from the second read on: two stuck reads agree, but never decide.
    Rig<MinimalPort> r;
    REQUIRE(r.Start());
    REQUIRE(r.sensor.MeasureOnce().ok());
    r.Bus().line = bmp581_test::LineFault::StuckLow;
    r.Bus().line_from = r.Next() + 2;
    CHECK_EQ(r.sensor.ReadMeasurement(bmp581::Verify::ReadBack).error, Error::InvalidReply);
  }
  {
    // MeasureOnce() with the second read, and a failing second read.
    Rig<MinimalSpiPort> r;
    REQUIRE(r.Start());
    const auto m = r.sensor.MeasureOnce(bmp581::Verify::ReadBack);
    REQUIRE(m.ok());
    CHECK_EQ(m.value.pressure_raw, r.Chip().pressure_raw);
    CHECK_EQ(r.sensor.MeasureOnce(bmp581_test::NotAnEnumerator<bmp581::Verify>(7)).error,
             Error::InvalidArgument);
    r.Bus().fail_transfer = r.Next() + 2;
    CHECK_EQ(r.sensor.ReadMeasurement(bmp581::Verify::ReadBack).error, Error::Bus);
  }
  {
    // Continuous mode: a new measurement between the reads is taken from the later two (NoData
    // only before the first measurement).
    Rig<MinimalPort> r(bmp581::HighResolutionPreset());
    REQUIRE(r.Start());
    REQUIRE(r.sensor.SetPowerMode(PowerMode::Continuous).ok());
    for (int i = 0; i < 50; ++i) {
      r.Chip().pressure_raw = RawPa(100'000 + i);
      r.Chip().Elapse(997);
      const auto m = r.sensor.ReadMeasurement(bmp581::Verify::ReadBack);
      CHECK(m.ok() || ((m.error == Error::NoData) && (r.Chip().measurements == 0)));
    }
  }
}

// --- Start-up -----------------------------------------------------------------------------------

TEST_CASE("After a failed first start that wrote, standby is never refused") {
  for (int k = 6; k < 40; ++k) {
    Rig<MinimalPort> r(bmp581::LowPowerPreset());
    r.Bus().fail_from = k;
    if (r.sensor.EnsureInitialized().ok()) {
      break; // the start-up ended before transfer k
    }
    r.Bus().fail_from = -1;
    CHECK(r.sensor.SetPowerMode(PowerMode::Standby).ok());
    CHECK_EQ(r.Chip().Mode(), ModelMode::Standby);
  }
}

} // namespace
