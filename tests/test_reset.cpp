/**
 * @file test_reset.cpp
 * @brief A power-on reset mid-session is always found, confirmed before the driver acts, and
 * never written over (hardware lessons 12a, 12c, 12d). The model resets on its own: registers back
 * to their reset values, INT_STATUS.por set, the chip silent for its boot time.
 */
#include <array>
#include <cstdint>

#include "fixture.hpp"
#include "test_harness.hpp"

using bmp581::Config;
using bmp581::Error;
using bmp581::FifoSample;
using bmp581::PowerMode;
using bmp581_test::LineFault;
using bmp581_test::MinimalPort;
using bmp581_test::MinimalSpiPort;
using bmp581_test::ModelMode;
using bmp581_test::Rig;
namespace chip = bmp581_test::chip;
namespace reg = bmp581::reg;

namespace {

/// A started driver whose chip then went through a power-on reset and finished booting.
template <typename P>
void ResetAfterStart(Rig<P>& r) {
  r.Chip().PowerOnReset();
  r.Chip().Elapse(5000);
}

/// The driver found the reset: uninitialised, nothing written over the reset chip.
template <typename P>
void Found(Rig<P>& r, Error e) {
  CHECK_EQ(e, Error::DeviceReset);
  CHECK(!r.sensor.IsInitialized());
  CHECK(!r.sensor.IsConfigKnown());
  CHECK_EQ(r.Chip().writes_over_reset, 0);
  CHECK_EQ(r.sensor.ReadMeasurement().error, Error::NotInitialized);
  // EnsureInitialized() starts it again from the soft reset.
  CHECK(r.Start());
  CHECK_EQ(r.Chip().writes_over_reset, 0);
  CHECK((r.Chip().Reg(chip::kOsrConfig) & chip::kPressEn) != 0U);
}

template <typename P>
void EveryCallFindsTheReset() {
  {
    Rig<P> r;
    REQUIRE(r.Start());
    REQUIRE(r.sensor.MeasureOnce().ok());
    ResetAfterStart(r);
    Found(r, r.sensor.ReadMeasurement().error); // INT_STATUS.por in the data frame
  }
  {
    Rig<P> r;
    REQUIRE(r.Start());
    ResetAfterStart(r);
    Found(r, r.sensor.MeasureOnce().error); // the reset check before the forced write
  }
  {
    Rig<P> r;
    REQUIRE(r.Start());
    ResetAfterStart(r);
    Found(r, r.sensor.ApplyConfig(bmp581::HighResolutionPreset()).error);
  }
  {
    Rig<P> r;
    REQUIRE(r.Start());
    ResetAfterStart(r);
    Found(r, r.sensor.SetPowerMode(PowerMode::Normal).error);
  }
  {
    Rig<P> r(bmp581::LowPowerPreset());
    REQUIRE(r.Start());
    ResetAfterStart(r);
    Found(r, r.sensor.WaitDataReady().error);
  }
  {
    Rig<P> r;
    REQUIRE(r.Start());
    ResetAfterStart(r);
    Found(r, r.sensor.ReadInterruptStatus().error);
  }
  {
    Rig<P> r(bmp581::FifoPreset());
    REQUIRE(r.Start());
    ResetAfterStart(r);
    std::array<FifoSample, 4> out{};
    Found(r, r.sensor.ReadFifo(out).error); // empty FIFO: the reset check
  }
  {
    Rig<P> r(bmp581::FifoPreset());
    REQUIRE(r.Start());
    ResetAfterStart(r);
    // An empty FIFO with the FIFO on is checked: a reset empties and disables it.
    Found(r, r.sensor.ReadFifoCount().error);
  }
  {
    Rig<P> r;
    REQUIRE(r.Start());
    ResetAfterStart(r);
    Found(r, r.sensor.ReadRegister(reg::kIntStatus).error);
  }
  {
    Rig<P> r;
    REQUIRE(r.Start());
    ResetAfterStart(r);
    Found(r, r.sensor.CheckForReset().error);
  }
}

TEST_CASE("Every call that reads the chip finds a power-on reset, on I2C and SPI") {
  EveryCallFindsTheReset<MinimalPort>();
  EveryCallFindsTheReset<MinimalSpiPort>();
}

TEST_CASE("EnsureInitialized() on a running driver finds the reset and starts again") {
  Rig<MinimalPort> r(bmp581::LowPowerPreset());
  REQUIRE(r.Start());
  ResetAfterStart(r);
  CHECK_EQ(r.Chip().Mode(), ModelMode::Standby);
  CHECK(r.Start());
  CHECK_EQ(r.Chip().Mode(), ModelMode::Normal);
  CHECK_EQ(r.Chip().soft_resets, 2);
  CHECK_EQ(r.Chip().writes_over_reset, 0);
}

TEST_CASE("Reset evidence is confirmed by a second read before the driver acts") {
  // One corrupt read shows press_en = 0; the second read shows the configured chip: no reset.
  Rig<MinimalPort> r;
  REQUIRE(r.Start());
  const int reads = r.Bus().ReadsFrom(chip::kOsrConfig);
  r.Bus().corrupt_read = r.Next();
  r.Bus().corrupt_mask = chip::kPressEn;
  CHECK(r.sensor.CheckForReset().ok());
  CHECK(r.sensor.IsInitialized());
  CHECK_EQ(r.Bus().ReadsFrom(chip::kOsrConfig) - reads, 2);
}

TEST_CASE("A corrupt INT_STATUS.por is InvalidReply, not a reset, and does not stick") {
  Rig<MinimalPort> r;
  REQUIRE(r.Start());
  REQUIRE(r.sensor.MeasureOnce().ok());
  r.Bus().corrupt_read = r.Next();
  r.Bus().corrupt_byte = 10; // INT_STATUS in the data frame
  r.Bus().corrupt_mask = chip::kPor;
  CHECK_EQ(r.sensor.ReadMeasurement().error, Error::InvalidReply);
  CHECK(r.sensor.IsInitialized());
  CHECK(r.sensor.GetLatchedInterrupts().power_on_reset); // the latch keeps what was read
  CHECK(r.sensor.ReadMeasurement().ok());
}

TEST_CASE("A reset whose por was lost is still found by the sentinel") {
  Rig<MinimalPort> r;
  REQUIRE(r.Start());
  REQUIRE(r.sensor.MeasureOnce().ok());
  ResetAfterStart(r);
  // The data read fails after the chip cleared INT_STATUS: por lost. The check is now pending.
  r.Bus().lost_ack_transfer = r.Next();
  CHECK_EQ(r.sensor.ReadMeasurement().error, Error::Bus);
  CHECK((r.Chip().PendingFlags() & chip::kPor) == 0U);
  CHECK_EQ(r.sensor.ReadMeasurement().error, Error::DeviceReset);
  CHECK_EQ(r.Chip().writes_over_reset, 0);
}

TEST_CASE("A pending check that cannot be read keeps every call from returning Ok") {
  Rig<MinimalPort> r;
  REQUIRE(r.Start());
  REQUIRE(r.sensor.MeasureOnce().ok());
  ResetAfterStart(r);
  r.Bus().fail_transfer = r.Next(); // the data read fails: the check is pending
  CHECK_EQ(r.sensor.ReadMeasurement().error, Error::Bus);
  r.Bus().fail_from = r.Next(); // and the check cannot be read
  CHECK_EQ(r.sensor.ReadMeasurement().error, Error::Bus);
  CHECK_EQ(r.sensor.ReadInterruptStatus().error, Error::Bus);
  CHECK_EQ(r.sensor.ReadRegister(reg::kChipId).error, Error::Bus);
  CHECK_EQ(r.sensor.CheckForReset().error, Error::Bus);
  CHECK(r.sensor.IsInitialized());
  r.Bus().fail_from = -1;
  CHECK_EQ(r.sensor.ReadRegister(reg::kChipId).error, Error::DeviceReset);
  CHECK_EQ(r.Chip().writes_over_reset, 0);
}

TEST_CASE("A failed call keeps its own error; a confirmed reset outranks it") {
  Rig<MinimalPort> r;
  REQUIRE(r.Start());
  ResetAfterStart(r);
  // The data registers hold 0x7F7F7F (NoData) and INT_STATUS shows por: the reset wins.
  CHECK_EQ(r.sensor.ReadMeasurement().error, Error::DeviceReset);
}

TEST_CASE("The driver's own writes never erase the reset sentinel") {
  const std::array<Config, 5> configs{Config{}, bmp581::ForcedPreset(), bmp581::LowPowerPreset(),
                                      bmp581::HighResolutionPreset(), bmp581::FifoPreset()};
  for (const Config& c : configs) {
    Rig<MinimalPort> r(c);
    REQUIRE(r.Start());
    CHECK((r.Chip().Reg(chip::kOsrConfig) & chip::kPressEn) != 0U);
    (void)r.sensor.SetPowerMode(PowerMode::Continuous);
    (void)r.sensor.SetPowerMode(PowerMode::Standby);
    (void)r.sensor.MeasureOnce();
    (void)r.sensor.ApplyConfig(c);
    CHECK((r.Chip().Reg(chip::kOsrConfig) & chip::kPressEn) != 0U);
  }
}

/// A reset before every transfer of ApplyConfig in turn: the call is never Ok, and no write
/// lands on the reset chip unless the driver has already seen the reset.
template <typename P>
void ResetDuringApplyConfig(uint32_t standby_delay_us, bool disabled_flags, uint32_t busy_us) {
  for (int k = 0; k < 64; ++k) {
    Rig<P> r(bmp581::LowPowerPreset());
    r.Chip().standby_delay_us = standby_delay_us;
    r.Chip().reset_busy_us = busy_us;
    r.Chip().flags_for_disabled_sources = disabled_flags;
    REQUIRE(r.Start());
    const int first = r.Next();
    r.Bus().reset_before = first + k;
    const auto result = r.sensor.ApplyConfig(bmp581::HighResolutionPreset());
    const bool reset_inside = r.Chip().resets > 1;
    if (!reset_inside) {
      CHECK(result.ok()); // the call ended before transfer k
      continue;
    }
    CHECK(!result.ok());
    // The reset is never absorbed: the driver is uninitialised, or the next check finds it.
    r.Chip().Elapse(5000);
    if (r.sensor.IsInitialized()) {
      CHECK_EQ(r.sensor.CheckForReset().error, Error::DeviceReset);
    }
    CHECK(r.Start());
  }
}

TEST_CASE("A reset at any point of ApplyConfig() is never reported Ok (both model readings)") {
  ResetDuringApplyConfig<MinimalPort>(1000, false, 1500);
  ResetDuringApplyConfig<MinimalPort>(0, true, 1500);
  ResetDuringApplyConfig<MinimalSpiPort>(1000, false, 1500);
  // A chip that answers at once after the reset: the sequence's own OSR_CONFIG write may cover it.
  ResetDuringApplyConfig<MinimalPort>(1000, false, 0);
  ResetDuringApplyConfig<MinimalPort>(0, true, 0);
}

TEST_CASE("A reset hidden by a failed configuration sequence is still found by the next read") {
  // The reset lands among the configuration writes; the chip answers at once, the sequence
  // rewrites OSR_CONFIG (when deep standby takes writes), and a read-back mismatches. The por is
  // all that is left besides the marker. Both readings of deep standby.
  for (const bool drops : {true, false}) {
    for (int k = 3; k < 32; ++k) {
      Rig<MinimalPort> r;
      r.Chip().reset_busy_us = 0;
      r.Chip().deep_standby_drops_writes = drops;
      REQUIRE(r.Start());
      REQUIRE(r.sensor.MeasureOnce().ok());
      r.Bus().reset_before = r.Next() + k;
      const auto result = r.sensor.ApplyConfig(bmp581::HighResolutionPreset());
      if (r.Chip().resets < 2) {
        CHECK(result.ok()); // the call ended before transfer k
        continue;
      }
      CHECK(!result.ok());
      if (r.sensor.IsInitialized()) {
        CHECK(!r.sensor.ReadMeasurement().ok());
        CHECK(!r.sensor.ApplyConfig(bmp581::HighResolutionPreset()).ok() ||
              !r.Chip().por_since_soft);
      }
      CHECK(r.Start());
    }
  }
}

TEST_CASE("A reset during a forced measurement is found by the polls") {
  Config c{};
  c.pressure_oversampling = bmp581::Oversampling::X64;
  Rig<MinimalPort> r(c);
  REQUIRE(r.Start());
  r.Bus().reset_before = r.Next() + 5; // INT_STATUS, sentinel, write, started, first poll...
  const auto m = r.sensor.MeasureOnce();
  CHECK(!m.ok());
  r.Chip().Elapse(5000);
  CHECK(!r.sensor.CheckForReset().ok() || !r.sensor.IsInitialized());
  CHECK_EQ(r.Chip().writes_over_reset, 0);
}

/// A reset whose OSR_CONFIG sentinel was rewritten afterwards (a sequence whose reads were
/// disturbed): only INT_STATUS.por and the rest of the configuration are left.
template <typename P>
void SentinelRewritten(Rig<P>& r) {
  ResetAfterStart(r);
  r.Chip().SetReg(chip::kOsrConfig,
                  bmp581::detail::OsrConfigValue(r.sensor.GetConfig())); // not a driver write
}

TEST_CASE(
    "A setter checks INT_STATUS before it writes: a rewritten sentinel does not hide a reset") {
  {
    Rig<MinimalPort> r(bmp581::HighResolutionPreset());
    REQUIRE(r.Start());
    SentinelRewritten(r);
    const int writes = r.Chip().writes_over_reset;
    CHECK_EQ(r.sensor.ApplyConfig(bmp581::HighResolutionPreset()).error, Error::DeviceReset);
    CHECK_EQ(r.Chip().writes_over_reset, writes);
  }
  {
    Rig<MinimalPort> r(bmp581::FifoPreset());
    REQUIRE(r.Start());
    SentinelRewritten(r);
    CHECK_EQ(r.sensor.SetPowerMode(PowerMode::Continuous).error, Error::DeviceReset);
    CHECK_EQ(r.Chip().writes_over_reset, 0);
  }
  {
    Config c = bmp581::ForcedPreset();
    c.pressure_iir = bmp581::IirFilter::Coef7;
    Rig<MinimalPort> r(c);
    REQUIRE(r.Start());
    SentinelRewritten(r);
    CHECK_EQ(r.sensor.MeasureOnce().error, Error::DeviceReset);
    CHECK_EQ(r.Chip().writes_over_reset, 0);
  }
  {
    // The configuration equals the reset values but for press_en: the chip holds the whole
    // configuration, and only the marker in NVM_DATA, which the reset cleared, tells.
    Rig<MinimalPort> r;
    REQUIRE(r.Start());
    SentinelRewritten(r);
    CHECK_EQ(r.sensor.ApplyConfig(Config{}).error, Error::DeviceReset);
    CHECK_EQ(r.Chip().writes_over_reset, 0);
    CHECK(r.Start());
  }
  for (int k = 2; k < 5; ++k) {
    // A read that decides cannot be read (either block, or the marker): the decision moves to the
    // next call, which still writes nothing over the reset chip.
    Rig<MinimalPort> r;
    REQUIRE(r.Start());
    SentinelRewritten(r);
    r.Bus().fail_transfer = r.Next() + k; // INT_STATUS, sentinel, two configuration reads, marker
    CHECK_EQ(r.sensor.ApplyConfig(Config{}).error, Error::Bus);
    CHECK_EQ(r.sensor.ApplyConfig(Config{}).error, Error::DeviceReset);
    CHECK_EQ(r.Chip().writes_over_reset, 0);
  }
  {
    // The configuration read that decides cannot be read: the check stays due.
    Rig<MinimalPort> r(bmp581::HighResolutionPreset());
    REQUIRE(r.Start());
    SentinelRewritten(r);
    r.Bus().fail_transfer = r.Next() + 2; // INT_STATUS, sentinel, the read that confirms it
    CHECK_EQ(r.sensor.ApplyConfig(bmp581::HighResolutionPreset()).error, Error::Bus);
    CHECK_EQ(r.sensor.ApplyConfig(bmp581::HighResolutionPreset()).error, Error::DeviceReset);
    CHECK_EQ(r.Chip().writes_over_reset, 0);
  }
}

TEST_CASE("Standby and Deinitialize() are never refused after a reset or with doubt") {
  Rig<MinimalPort> r(bmp581::LowPowerPreset());
  REQUIRE(r.Start());
  ResetAfterStart(r);
  CHECK_EQ(r.sensor.CheckForReset().error, Error::DeviceReset);
  CHECK(!r.sensor.IsInitialized());
  // Uninitialised after the reset: standby is still sent, and confirmed.
  const int writes = r.Bus().WritesTo(chip::kOdrConfig);
  CHECK(r.sensor.SetPowerMode(PowerMode::Standby).ok());
  CHECK_EQ(r.Bus().WritesTo(chip::kOdrConfig) - writes, 1);
  CHECK(r.sensor.Deinitialize().ok());
  // With the configuration in doubt (a failed write), standby still goes out.
  Rig<MinimalPort> d(bmp581::LowPowerPreset());
  REQUIRE(d.Start());
  d.Bus().nack_transfer = d.Next() + 7; // a configuration write of ApplyConfig
  CHECK_EQ(d.sensor.ApplyConfig(bmp581::HighResolutionPreset()).error, Error::Bus);
  CHECK(!d.sensor.IsConfigKnown());
  CHECK(d.sensor.SetPowerMode(PowerMode::Standby).ok());
  CHECK_EQ(d.Chip().Mode(), ModelMode::Standby);
  // With a reset check pending that cannot pass yet.
  Rig<MinimalPort> p(bmp581::LowPowerPreset());
  REQUIRE(p.Start());
  p.Bus().corrupt_read = p.Next() + 1; // the confirmation read of standby
  p.Bus().corrupt_mask = 0x80;         // a reserved bit: read once more, then Ok
  CHECK_EQ(p.sensor.SetPowerMode(PowerMode::Standby).error, Error::Ok);
  p.Bus().fail_transfer = p.Next() + 1; // the confirmation cannot be read: the check stays due
  CHECK_EQ(p.sensor.SetPowerMode(PowerMode::Standby).error, Error::Bus);
  CHECK_EQ(p.sensor.SetPowerMode(PowerMode::Standby).error, Error::Ok);
}

} // namespace
