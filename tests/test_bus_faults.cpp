/**
 * @file test_bus_faults.cpp
 * @brief Stuck, floating, looped-back and deaf lines, NACKs, lost acknowledgements, dropped
 * writes and corrupt reads (hardware lessons 1 to 6, 12c; handbook KB-CPP-116, -117, -130). No
 * reply a broken line can produce is ever returned as data.
 */
#include <array>
#include <cstdint>
#include <span>

#include "fixture.hpp"
#include "test_harness.hpp"

using bmp581::Config;
using bmp581::Error;
using bmp581::FifoSample;
using bmp581::PowerMode;
using bmp581_test::FullPort;
using bmp581_test::LineFault;
using bmp581_test::MinimalPort;
using bmp581_test::MinimalSpiPort;
using bmp581_test::Rig;
namespace chip = bmp581_test::chip;
namespace reg = bmp581::reg;

namespace {

template <typename P>
void NoStartOnABrokenLine(LineFault fault) {
  Rig<P> r;
  r.Bus().line = fault;
  CHECK(!r.sensor.EnsureInitialized().ok());
  CHECK(!r.sensor.IsInitialized());
}

TEST_CASE("No start-up succeeds on a stuck, floating, looped-back or deaf line") {
  for (const LineFault f :
       {LineFault::StuckLow, LineFault::FloatHigh, LineFault::Echo, LineFault::Deaf}) {
    NoStartOnABrokenLine<MinimalPort>(f);
    NoStartOnABrokenLine<MinimalSpiPort>(f);
  }
}

/// After a good start the line breaks: no call returns a measurement or a sample.
template <typename P>
void NothingIsDataOnABrokenLine(LineFault fault) {
  Config c = bmp581::FifoPreset();
  c.source_data_ready = true;
  Rig<P> r(c);
  REQUIRE(r.Start());
  r.Chip().Elapse(300'000);
  r.Bus().line = fault;
  r.Bus().line_from = r.Next();
  for (int i = 0; i < 3; ++i) {
    CHECK(!r.sensor.ReadMeasurement().ok());
    CHECK(!r.sensor.ReadMeasurement(bmp581::Verify::ReadBack).ok());
    std::array<FifoSample, 16> out{};
    const auto n = r.sensor.ReadFifo(out);
    CHECK(!n.ok() || (n.value == 0U));
    CHECK(!r.sensor.WaitDataReady().ok());
    CHECK(!r.sensor.ReadInterruptStatus().ok() || (fault == LineFault::StuckLow) ||
          (fault == LineFault::Echo));
  }
}

TEST_CASE("After the start, no read on a broken line is ever returned as data") {
  for (const LineFault f :
       {LineFault::StuckLow, LineFault::FloatHigh, LineFault::Echo, LineFault::Deaf}) {
    NothingIsDataOnABrokenLine<MinimalPort>(f);
    NothingIsDataOnABrokenLine<MinimalSpiPort>(f);
  }
}

TEST_CASE("A forced measurement on a broken line is never Ok") {
  for (const LineFault f : {LineFault::StuckLow, LineFault::FloatHigh, LineFault::Deaf}) {
    Rig<MinimalPort> r;
    REQUIRE(r.Start());
    r.Bus().line = f;
    r.Bus().line_from = r.Next();
    CHECK(!r.sensor.MeasureOnce().ok());
  }
}

TEST_CASE("A line that breaks during the data read is never data: each byte position") {
  for (std::size_t byte = 0; byte < 12U; ++byte) {
    Rig<MinimalPort> r;
    REQUIRE(r.Start());
    REQUIRE(r.sensor.MeasureOnce().ok());
    r.Bus().corrupt_read = r.Next();
    r.Bus().corrupt_byte = byte;
    r.Bus().corrupt_mask = 0xE0;
    const auto m = r.sensor.ReadMeasurement();
    if (byte >= 6U) {
      CHECK_EQ(m.error, Error::InvalidReply); // a reserved register or reserved bits
    } else {
      CHECK(!m.ok() || (m.value.pressure_raw != r.Chip().pressure_raw) ||
            (m.value.temperature_raw != r.Chip().temperature_raw));
    }
    CHECK(r.sensor.ReadMeasurement().ok()); // one corrupt read does not stick
  }
}

TEST_CASE("An address NACK is NoDevice with GetLastFailure(), Bus without") {
  Rig<FullPort> full;
  full.Bus().fail_from = 0;
  CHECK_EQ(full.sensor.EnsureInitialized().error, Error::NoDevice);
  full.port.failure_known = false;
  CHECK_EQ(full.sensor.EnsureInitialized().error, Error::Bus);
  Rig<MinimalPort> minimal;
  minimal.Bus().fail_from = 0;
  CHECK_EQ(minimal.sensor.EnsureInitialized().error, Error::Bus);
  Rig<MinimalSpiPort> spi;
  spi.Bus().fail_from = 0;
  CHECK_EQ(spi.sensor.EnsureInitialized().error, Error::Bus);
  Rig<FullPort> later;
  REQUIRE(later.Start());
  later.Bus().fail_transfer = later.Next();
  CHECK_EQ(later.sensor.ReadMeasurement().error, Error::NoDevice);
}

TEST_CASE("A failure at each start-up transfer leaves the driver uninitialised, then it starts") {
  for (int k = 0; k < 30; ++k) {
    Rig<MinimalPort> r(bmp581::LowPowerPreset());
    r.Bus().fail_transfer = k;
    const auto e = r.sensor.EnsureInitialized();
    if (!e.ok()) {
      CHECK(!r.sensor.IsInitialized());
      CHECK(r.Start());
    }
  }
  for (int k = 0; k < 30; ++k) {
    Rig<MinimalSpiPort> r(bmp581::FifoPreset());
    r.Bus().fail_transfer = k;
    if (!r.sensor.EnsureInitialized().ok()) {
      CHECK(r.Start());
    }
  }
}

TEST_CASE("A write that may have landed leaves the configuration in doubt until it is applied") {
  for (int k = 0; k < 25; ++k) {
    Rig<MinimalPort> r;
    REQUIRE(r.Start());
    const int index = r.Next() + k;
    r.Bus().lost_ack_transfer = index;
    const auto e = r.sensor.ApplyConfig(bmp581::HighResolutionPreset());
    if (e.ok() || !r.Bus().log[static_cast<std::size_t>(index)].write) {
      continue;
    }
    CHECK(!r.sensor.IsConfigKnown());
    if (r.sensor.IsInitialized()) {
      // A write before the configuration writes (the standby, a mode): the doubt is kept.
      CHECK_EQ(r.sensor.MeasureOnce().error, Error::StateUnknown);
      CHECK_EQ(r.sensor.ReadFifoCount().error, Error::StateUnknown);
      CHECK_EQ(r.sensor.SetPowerMode(PowerMode::Normal).error, Error::StateUnknown);
      CHECK(r.sensor.ApplyConfig(bmp581::HighResolutionPreset()).ok());
    } else {
      // The configuration writes had started: a reset could hide behind them, so the driver starts
      // again from the soft reset (the standby is still sent).
      CHECK_EQ(r.sensor.MeasureOnce().error, Error::NotInitialized);
      CHECK(r.sensor.SetPowerMode(PowerMode::Standby).ok());
      CHECK(r.Start());
    }
    CHECK(r.sensor.IsConfigKnown());
  }
}

TEST_CASE("A dropped write never leaves Ok with a chip that does not hold the configuration") {
  const Config want = bmp581::HighResolutionPreset();
  for (int k = 0; k < 25; ++k) {
    Rig<MinimalSpiPort> r;
    REQUIRE(r.Start());
    r.Bus().drop_transfer = r.Next() + k;
    const auto e = r.sensor.ApplyConfig(want);
    if (!e.ok()) {
      CHECK(!r.sensor.IsConfigKnown());
      continue;
    }
    CHECK_EQ(r.Chip().Reg(chip::kOsrConfig), bmp581::detail::OsrConfigValue(want));
    CHECK_EQ(r.Chip().Reg(chip::kOdrConfig) & 0xFCU,
             bmp581::detail::OdrConfigValue(want, PowerMode::Standby));
    CHECK_EQ(r.Chip().Reg(chip::kDspIir), bmp581::detail::DspIirValue(want));
    CHECK_EQ(r.Chip().Reg(chip::kDspConfig), bmp581::detail::DspConfigValue(want));
    CHECK_EQ(r.Chip().Reg(chip::kIntSource), bmp581::detail::IntSourceValue(want));
  }
}

TEST_CASE("A dropped forced-mode write is WriteMismatch: the old data is never returned as new") {
  Rig<MinimalPort> r;
  REQUIRE(r.Start());
  REQUIRE(r.sensor.MeasureOnce().ok());
  r.Bus().drop_transfer = r.Next() + 2; // INT_STATUS, sentinel, then the forced write
  CHECK_EQ(r.sensor.MeasureOnce().error, Error::WriteMismatch);
  CHECK(r.sensor.MeasureOnce().ok());
}

TEST_CASE("A mode change whose write or read-back fails leaves the configuration in doubt") {
  Rig<MinimalPort> r;
  REQUIRE(r.Start());
  r.Bus().lost_ack_transfer = r.Next() + 2; // INT_STATUS, sentinel, then the mode write
  CHECK_EQ(r.sensor.SetPowerMode(PowerMode::Normal).error, Error::Bus);
  CHECK(!r.sensor.IsConfigKnown());
  Rig<MinimalPort> b;
  REQUIRE(b.Start());
  b.Bus().fail_transfer = b.Next() + 3; // the read-back
  CHECK_EQ(b.sensor.SetPowerMode(PowerMode::Normal).error, Error::Bus);
  CHECK(!b.sensor.IsConfigKnown());
  Rig<MinimalPort> c;
  REQUIRE(c.Start());
  c.Bus().corrupt_read = c.Next() + 3;
  c.Bus().corrupt_byte = 1;
  c.Bus().corrupt_mask = 0x04; // the data rate of the read-back
  CHECK_EQ(c.sensor.SetPowerMode(PowerMode::Normal).error, Error::WriteMismatch);
  Rig<MinimalPort> d;
  REQUIRE(d.Start());
  d.Bus().corrupt_read = d.Next() + 3;
  d.Bus().corrupt_mask = 0x80; // a reserved bit of OSR_CONFIG in the read-back
  CHECK_EQ(d.sensor.SetPowerMode(PowerMode::Normal).error, Error::WriteMismatch);
  Rig<MinimalPort> e;
  REQUIRE(e.Start());
  e.Bus().corrupt_read = e.Next() + 3;
  e.Bus().corrupt_mask = chip::kPressEn; // press_en 0 once: confirmed clean, still not Ok
  CHECK_EQ(e.sensor.SetPowerMode(PowerMode::Normal).error, Error::WriteMismatch);
}

TEST_CASE("A corrupt read-back of the configuration is WriteMismatch or a confirmed check") {
  for (std::size_t byte = 0; byte < 8U; ++byte) {
    Rig<MinimalPort> r;
    REQUIRE(r.Start());
    // ApplyConfig: INT_STATUS, sentinel, ODR_CONFIG out of deep standby, NVM_DATA, INT_SOURCE,
    // INT_STATUS, 12 writes, block A, block B.
    r.Bus().corrupt_read = r.Next() + 19;
    r.Bus().corrupt_byte = byte;
    r.Bus().corrupt_mask = (byte == 6U) ? chip::kPressEn : uint8_t{0x01};
    CHECK_EQ(r.sensor.ApplyConfig(Config{}).error, Error::WriteMismatch);
    CHECK(r.sensor.ApplyConfig(Config{}).ok());
  }
  for (std::size_t byte = 0; byte < 5U; ++byte) {
    Rig<MinimalPort> r;
    REQUIRE(r.Start());
    r.Bus().corrupt_read = r.Next() + 18;
    r.Bus().corrupt_byte = byte;
    r.Bus().corrupt_mask = 0x80;
    CHECK_EQ(r.sensor.ApplyConfig(Config{}).error, Error::WriteMismatch);
  }
}

TEST_CASE("A corrupt reset-check read is read again, and a mismatch that stays is a reset") {
  Rig<MinimalPort> r;
  REQUIRE(r.Start());
  const int reads = r.Bus().ReadsFrom(chip::kOsrConfig);
  r.Bus().corrupt_read = r.Next();
  r.Bus().corrupt_mask = 0x08; // an oversampling bit differs from the copy once
  CHECK(r.sensor.CheckForReset().ok());
  CHECK_EQ(r.Bus().ReadsFrom(chip::kOsrConfig) - reads, 2);
  CHECK(r.sensor.IsInitialized());
  // The chip itself no longer holds the configuration (something rewrote it): confirmed, a reset.
  r.Chip().SetReg(chip::kOsrConfig, static_cast<uint8_t>(r.Chip().Reg(chip::kOsrConfig) ^ 0x08U));
  CHECK_EQ(r.sensor.CheckForReset().error, Error::DeviceReset);
  CHECK(r.Start());
  // A floating reserved bit in the confirmation read.
  Rig<MinimalPort> f;
  REQUIRE(f.Start());
  f.Bus().corrupt_read = f.Next();
  f.Bus().corrupt_mask = chip::kPressEn;
  f.Bus().fail_transfer = f.Next() + 1;
  CHECK_EQ(f.sensor.CheckForReset().error, Error::Bus);
  CHECK(f.sensor.CheckForReset().ok());
}

TEST_CASE("INT_STATUS with reserved bits set is InvalidReply in every reader") {
  Config c = bmp581::LowPowerPreset();
  Rig<MinimalPort> r(c);
  REQUIRE(r.Start());
  r.Bus().corrupt_read = r.Next();
  r.Bus().corrupt_mask = 0x80;
  CHECK_EQ(r.sensor.WaitDataReady().error, Error::InvalidReply);
  r.Bus().corrupt_read = r.Next();
  CHECK_EQ(r.sensor.ReadInterruptStatus().error, Error::InvalidReply);
  r.Bus().corrupt_read =
      r.Next() + 6; // INT_STATUS, sentinel, standby, sentinel, NVM_DATA, INT_SOURCE, INT_STATUS
  CHECK_EQ(r.sensor.ApplyConfig(c).error, Error::InvalidReply);
  CHECK(r.sensor.ApplyConfig(c).ok());
  r.Bus().fail_transfer = r.Next() + 3;
  CHECK_EQ(r.sensor.ApplyConfig(c).error, Error::Bus);
  CHECK(r.sensor.ApplyConfig(c).ok());
}

TEST_CASE("A FIFO_COUNT that cannot be is InvalidReply; a stuck FIFO burst is InvalidReply") {
  Rig<MinimalPort> r(bmp581::FifoPreset());
  REQUIRE(r.Start());
  r.Chip().Elapse(300'000);
  r.Bus().corrupt_read = r.Next();
  r.Bus().corrupt_mask = 0x40; // reserved bit 6
  CHECK_EQ(r.sensor.ReadFifoCount().error, Error::InvalidReply);
  r.Bus().corrupt_read = r.Next();
  r.Bus().corrupt_mask = 0x10; // 16 or more frames more than were there: above 16 PT frames
  const auto over = r.sensor.ReadFifoCount();
  CHECK(!over.ok() || (over.value <= 16U));
  r.Bus().line = LineFault::StuckLow;
  r.Bus().line_from = r.Next() + 1; // the count reads, then the burst is stuck
  std::array<FifoSample, 16> out{};
  // A line stuck low reads OSR_CONFIG = 0x00 in the check too: a reset, the safe direction.
  const Error stuck = r.sensor.ReadFifo(out).error;
  CHECK((stuck == Error::InvalidReply) || (stuck == Error::DeviceReset));
  Rig<MinimalPort> f(bmp581::FifoPreset());
  REQUIRE(f.Start());
  f.Chip().Elapse(300'000);
  f.Bus().line = LineFault::FloatHigh;
  f.Bus().line_from = f.Next() + 1;
  CHECK_EQ(f.sensor.ReadFifo(out).error, Error::InvalidReply);
  Rig<MinimalPort> b(bmp581::FifoPreset());
  REQUIRE(b.Start());
  b.Chip().Elapse(300'000);
  b.Bus().fail_transfer = b.Next() + 1;
  CHECK_EQ(b.sensor.ReadFifo(out).error, Error::Bus);
  b.Bus().fail_transfer = b.Next();
  CHECK_EQ(b.sensor.ReadFifo(out).error, Error::Bus);
}

TEST_CASE("A FIFO that should be off but counts frames is InvalidReply") {
  Rig<MinimalPort> r;
  REQUIRE(r.Start());
  r.Bus().corrupt_read = r.Next();
  r.Bus().corrupt_mask = 0x01;
  CHECK_EQ(r.sensor.ReadFifoCount().error, Error::InvalidReply);
}

TEST_CASE("Polls that fail stop at once with the bus error") {
  Rig<MinimalPort> nvm;
  nvm.Bus().fail_transfer = 3; // ODR_CONFIG, CMD, CHIP_ID, STATUS
  CHECK_EQ(nvm.sensor.EnsureInitialized().error, Error::Bus);
  Rig<MinimalPort> status;
  status.Bus().corrupt_read = 3;
  status.Bus().corrupt_mask = 0x20; // STATUS reserved bit
  CHECK_EQ(status.sensor.EnsureInitialized().error, Error::InvalidReply);
  Rig<MinimalPort> ints;
  ints.Bus().fail_transfer = 4;
  CHECK_EQ(ints.sensor.EnsureInitialized().error, Error::Bus);
  Rig<MinimalPort> ints2;
  ints2.Bus().fail_transfer = 5;
  CHECK_EQ(ints2.sensor.EnsureInitialized().error, Error::Bus);
  Rig<MinimalPort> forced;
  REQUIRE(forced.Start());
  forced.Bus().fail_transfer = forced.Next() + 4; // the first poll
  CHECK_EQ(forced.sensor.MeasureOnce().error, Error::Bus);
  forced.Chip().Elapse(200'000); // the forced measurement started by that call ends
  forced.Bus().fail_transfer = forced.Next() + 2; // the forced write
  CHECK_EQ(forced.sensor.MeasureOnce().error, Error::Bus);
  forced.Chip().Elapse(200'000);
  forced.Bus().fail_transfer = forced.Next() + 3; // the read after the write
  CHECK_EQ(forced.sensor.MeasureOnce().error, Error::Bus);
  forced.Chip().Elapse(200'000);
  forced.Bus().fail_transfer = forced.Next();
  CHECK_EQ(forced.sensor.MeasureOnce().error, Error::Bus);
  CHECK(forced.sensor.MeasureOnce().ok());
  Rig<MinimalPort> wait(bmp581::LowPowerPreset());
  REQUIRE(wait.Start());
  wait.Bus().fail_transfer = wait.Next();
  CHECK_EQ(wait.sensor.WaitDataReady().error, Error::Bus);
}

TEST_CASE("A standby whose write or check fails is reported, and leaves the mode in doubt") {
  Rig<MinimalPort> r(bmp581::LowPowerPreset());
  REQUIRE(r.Start());
  r.Bus().fail_transfer = r.Next();
  CHECK_EQ(r.sensor.SetPowerMode(PowerMode::Standby).error, Error::Bus);
  CHECK(!r.sensor.IsConfigKnown());
  r.Bus().fail_transfer = r.Next() + 1;
  CHECK_EQ(r.sensor.SetPowerMode(PowerMode::Standby).error, Error::Bus);
  // Uninitialised (after Deinitialize()) the plain read-back is used.
  Rig<MinimalPort> u;
  REQUIRE(u.Start());
  REQUIRE(u.sensor.Deinitialize().ok());
  u.Bus().fail_transfer = u.Next() + 1;
  CHECK_EQ(u.sensor.SetPowerMode(PowerMode::Standby).error, Error::Bus);
  u.Chip().standby_never = true;
  u.Chip().SetReg(chip::kOdrConfig, 0x70);
  CHECK(u.sensor.SetPowerMode(PowerMode::Standby).ok());
  // ApplyConfig from normal mode: the standby write itself fails.
  Rig<MinimalPort> a(bmp581::LowPowerPreset());
  REQUIRE(a.Start());
  a.Bus().fail_transfer = a.Next() + 2; // INT_STATUS, sentinel, then standby
  CHECK_EQ(a.sensor.ApplyConfig(Config{}).error, Error::Bus);
  Rig<MinimalPort> s(bmp581::LowPowerPreset());
  REQUIRE(s.Start());
  s.Bus().fail_transfer = s.Next() + 2;
  CHECK_EQ(s.sensor.SetPowerMode(PowerMode::Continuous).error, Error::Bus);
}

TEST_CASE("Raw reads that fail keep INT_STATUS in doubt") {
  Rig<MinimalPort> r;
  REQUIRE(r.Start());
  r.Bus().fail_transfer = r.Next();
  CHECK_EQ(r.sensor.ReadRegister(reg::kIntStatus).error, Error::Bus);
  const int before = r.Bus().ReadsFrom(chip::kOsrConfig);
  CHECK(r.sensor.ReadRegister(reg::kChipId).ok());
  CHECK_EQ(r.Bus().ReadsFrom(chip::kOsrConfig) - before, 1); // the pending check ran first
  r.Bus().fail_transfer = r.Next();
  CHECK_EQ(r.sensor.ReadRegister(reg::kChipId).error, Error::Bus);
}

TEST_CASE("Calls that depend on the configuration refuse while it is in doubt") {
  Config c = bmp581::FifoPreset();
  c.source_data_ready = true;
  Rig<MinimalPort> r(c);
  REQUIRE(r.Start());
  r.Bus().lost_ack_transfer = r.Next() + 2; // INT_STATUS, sentinel, then the standby write
  CHECK_EQ(r.sensor.ApplyConfig(c).error, Error::Bus);
  REQUIRE(!r.sensor.IsConfigKnown());
  const int before = r.Next();
  std::array<FifoSample, 4> out{};
  CHECK_EQ(r.sensor.WaitDataReady().error, Error::StateUnknown);
  CHECK_EQ(r.sensor.ReadFifo(out).error, Error::StateUnknown);
  CHECK_EQ(r.Next(), before);
}

TEST_CASE("A forced measurement whose start cannot be confirmed is not Ok") {
  Rig<MinimalPort> r;
  REQUIRE(r.Start());
  r.Bus().fail_transfer =
      r.Next() + 3; // INT_STATUS, sentinel, forced write, the read that confirms it
  CHECK_EQ(r.sensor.MeasureOnce().error, Error::Bus);
}

TEST_CASE("A pending check that fails stops ApplyConfig() and ReadFifoCount()") {
  Rig<MinimalPort> r(bmp581::FifoPreset());
  REQUIRE(r.Start());
  r.Bus().fail_transfer = r.Next();
  CHECK_EQ(r.sensor.ReadRegister(reg::kIntStatus).error, Error::Bus); // check now due
  r.Bus().fail_transfer = r.Next();
  CHECK_EQ(r.sensor.ApplyConfig(bmp581::FifoPreset()).error, Error::Bus);
  r.Chip().Elapse(300'000); // frames in the FIFO: no empty-FIFO check
  r.Chip().RaiseFlags(chip::kPor);
  CHECK_EQ(r.sensor.ReadInterruptStatus().error, Error::InvalidReply);
  // The doubt moved to the next call: it checks again before it reads.
  const int before = r.Bus().ReadsFrom(chip::kOsrConfig);
  CHECK(r.sensor.ReadFifoCount().ok());
  CHECK_EQ(r.Bus().ReadsFrom(chip::kOsrConfig) - before, 1);
  CHECK(r.sensor.ReadFifoCount().ok());
  CHECK_EQ(r.Bus().ReadsFrom(chip::kOsrConfig) - before, 1);
}

TEST_CASE("INT_STATUS.por inside a configuration sequence is checked or taken as the reset") {
  // por before the writes, with the registers intact: InvalidReply, nothing written.
  Rig<MinimalPort> r;
  REQUIRE(r.Start());
  r.Chip().RaiseFlags(chip::kPor);
  CHECK_EQ(r.sensor.ApplyConfig(bmp581::HighResolutionPreset()).error, Error::InvalidReply);
  // A reset just before the last INT_STATUS read: the sequence rewrote the sentinel, so the
  // por is taken as the reset.
  Rig<MinimalPort> late;
  REQUIRE(late.Start());
  late.Chip().reset_busy_us = 0;
  late.Bus().reset_before = late.Next() + 18;
  CHECK_EQ(late.sensor.ApplyConfig(Config{}).error, Error::DeviceReset);
  CHECK(!late.sensor.IsInitialized());
}

TEST_CASE("The register framing refuses an empty or oversized burst with nothing sent") {
  MinimalPort i2c;
  bmp581::detail::I2cRegisterBus<MinimalPort> a(i2c);
  std::array<uint8_t, 97> big{};
  CHECK_EQ(a.Read(0x01, std::span<uint8_t>{}), Error::InvalidArgument);
  CHECK_EQ(a.Read(0x01, big), Error::InvalidArgument);
  CHECK_EQ(a.GetAddress(), bmp581::i2c::kI2cAddress);
  MinimalSpiPort spi;
  bmp581::detail::SpiRegisterBus<MinimalSpiPort> b(spi);
  CHECK_EQ(b.Read(0x01, std::span<uint8_t>{}), Error::InvalidArgument);
  CHECK_EQ(b.Read(0x01, big), Error::InvalidArgument);
  CHECK(b.AddressValid());
  CHECK_EQ(i2c.state.transfers + spi.state.transfers, 0);
  CHECK_EQ(bmp581::detail::FifoFrameBytes(bmp581::FifoFrame::Disabled), 0U);
  CHECK_EQ(bmp581::detail::FifoFrameBytes(bmp581_test::NotAnEnumerator<bmp581::FifoFrame>(9)), 0U);
}

TEST_CASE("The version getters follow the build settings") {
  // Called through pointers, so they run (and are measured) rather than fold at compile time.
  uint32_t (*major)() noexcept = &bmp581::GetDriverVersionMajor;
  uint32_t (*minor)() noexcept = &bmp581::GetDriverVersionMinor;
  uint32_t (*patch)() noexcept = &bmp581::GetDriverVersionPatch;
  const char* (*text)() noexcept = &bmp581::GetDriverVersion;
  CHECK_EQ(major(), static_cast<uint32_t>(BMP581_VERSION_MAJOR));
  CHECK_EQ(minor(), static_cast<uint32_t>(BMP581_VERSION_MINOR));
  CHECK_EQ(patch(), static_cast<uint32_t>(BMP581_VERSION_PATCH));
  CHECK(text()[0] == '0');
}

} // namespace
