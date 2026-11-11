/**
 * @file test_bmp581.cpp
 * @brief Lifecycle, configuration, measurement, FIFO and diagnostics over the register model, on
 * I2C and SPI. Failure paths of the bus are in test_bus_faults.cpp, resets in test_reset.cpp.
 */
#include <array>
#include <cstdint>
#include <span>
#include <string_view>

#include "fixture.hpp"
#include "test_harness.hpp"

using bmp581::Config;
using bmp581::Error;
using bmp581::FifoFrame;
using bmp581::FifoSample;
using bmp581::PowerMode;
using bmp581::Verify;
using bmp581_test::FullPort;
using bmp581_test::FullSpiPort;
using bmp581_test::MinimalPort;
using bmp581_test::MinimalSpiPort;
using bmp581_test::ModelMode;
using bmp581_test::NotAnEnumerator;
using bmp581_test::RawC;
using bmp581_test::RawPa;
using bmp581_test::Rig;
namespace chip = bmp581_test::chip;
namespace reg = bmp581::reg;

namespace {

// --- Lifecycle ----------------------------------------------------------------------------------

template <typename P>
void NoIoBeforeStart() {
  Rig<P> r;
  CHECK(!r.sensor.IsInitialized());
  CHECK_EQ(r.sensor.ReadMeasurement().error, Error::NotInitialized);
  CHECK_EQ(r.sensor.MeasureOnce().error, Error::NotInitialized);
  CHECK_EQ(r.sensor.WaitDataReady().error, Error::NotInitialized);
  CHECK_EQ(r.sensor.ReadInterruptStatus().error, Error::NotInitialized);
  CHECK_EQ(r.sensor.ReadFifoCount().error, Error::NotInitialized);
  std::array<FifoSample, 2> out{};
  CHECK_EQ(r.sensor.ReadFifo(out).error, Error::NotInitialized);
  CHECK_EQ(r.sensor.ReadRegister(reg::kChipId).error, Error::NotInitialized);
  CHECK_EQ(r.sensor.CheckForReset().error, Error::NotInitialized);
  CHECK_EQ(r.sensor.SetPowerMode(PowerMode::Normal).error, Error::NotInitialized);
  CHECK_EQ(r.sensor.SetPowerMode(PowerMode::Standby).error, Error::NotInitialized);
  CHECK_EQ(r.Bus().transfers, 0);
  CHECK_EQ(r.port.bus_up_calls, 0);
}

TEST_CASE("Nothing touches the chip before EnsureInitialized()") {
  NoIoBeforeStart<MinimalPort>();
  NoIoBeforeStart<MinimalSpiPort>();
}

template <typename P>
void StartsAndConfigures() {
  Rig<P> r;
  REQUIRE(r.Start());
  CHECK(r.sensor.IsInitialized());
  CHECK(r.sensor.IsConfigKnown());
  CHECK_EQ(r.Chip().soft_resets, 1);
  CHECK_EQ(r.sensor.GetRevision(), chip::kRevIdValue);
  CHECK_EQ(r.Chip().Mode(), ModelMode::Standby);
  // Every write is one register (stored whole or not at all).
  CHECK_EQ(r.Bus().LongestWrite(), 1U);
  // The reset values, except press_en: pressure is always measured.
  CHECK_EQ(r.Chip().Reg(chip::kOsrConfig), chip::kPressEn);
  CHECK_EQ(r.Chip().Reg(chip::kOdrConfig), chip::kOdrConfigReset);
  CHECK_EQ(r.Chip().Reg(chip::kIntConfig), chip::kIntConfigReset);
  CHECK_EQ(r.Chip().Reg(chip::kDspConfig), chip::kDspConfigReset);
  // t_powup and t_soft_res waited in full.
  CHECK(r.port.delayed_us >= (chip::kPowerUpMaxUs + chip::kSoftResetMaxUs));
  // The por of the driver's own reset is not in the latch.
  CHECK(!r.sensor.GetLatchedInterrupts().power_on_reset);
  CHECK_EQ(r.sensor.GetLastIntStatus(), 0U);
  CHECK((r.sensor.GetLastStatus() & chip::kNvmRdy) != 0U);
}

TEST_CASE("EnsureInitialized() resets, identifies and configures the chip, on I2C and SPI") {
  StartsAndConfigures<MinimalPort>();
  StartsAndConfigures<MinimalSpiPort>();
}

TEST_CASE("On SPI the first frame and the first after the soft reset are dummy reads") {
  Rig<MinimalSpiPort> r;
  REQUIRE(r.Start());
  CHECK(r.Chip().spi_selected);
  REQUIRE(r.Bus().log.size() > 3U);
  CHECK(!r.Bus().log[0].write);
  CHECK_EQ(r.Bus().log[0].reg, chip::kChipId);
  CHECK(r.Bus().log[1].write);
  CHECK_EQ(r.Bus().log[1].reg, chip::kOdrConfig); // out of deep standby before the reset
  CHECK(r.Bus().log[2].write);
  CHECK_EQ(r.Bus().log[2].reg, chip::kCmd);
  CHECK(!r.Bus().log[3].write);
  CHECK_EQ(r.Bus().log[3].reg, chip::kChipId);
}

TEST_CASE("On I2C the soft reset NACK is expected, and the reads prove the reset") {
  Rig<FullPort> r;
  REQUIRE(r.Start());
  CHECK(r.Bus().log[0].write);
  CHECK_EQ(r.Bus().log[0].reg, chip::kOdrConfig); // out of deep standby before the reset
  CHECK_EQ(r.Bus().log[1].reg, chip::kCmd);
  CHECK_EQ(r.port.error_lines, 0);
  CHECK(r.port.log_lines > 0);
}

TEST_CASE("A second EnsureInitialized() is one 3-byte reset check") {
  Rig<MinimalPort> r;
  REQUIRE(r.Start());
  const int before = r.Next();
  CHECK(r.Start());
  CHECK_EQ(r.Next() - before, 1);
  CHECK_EQ(r.Bus().log.back().reg, chip::kOsrConfig);
  CHECK_EQ(r.Bus().log.back().rx_len, 3U);
}

TEST_CASE("The SDO-high address works; any other address is refused with no I/O") {
  MinimalPort port;
  bmp581::BMP581<MinimalPort> high(port, bmp581::i2c::kI2cAddressSdoHigh);
  CHECK(high.EnsureInitialized().ok());
  MinimalPort other;
  bmp581::BMP581<MinimalPort> bad(other, uint8_t{0x76}, Config{});
  CHECK_EQ(bad.EnsureInitialized().error, Error::InvalidArgument);
  CHECK_EQ(bad.Deinitialize().error, Error::InvalidArgument);
  CHECK_EQ(other.state.transfers, 0);
  CHECK_EQ(other.bus_up_calls, 0);
}

TEST_CASE("An invalid Config is refused before any I/O") {
  Config c{};
  c.startup_timeout_ms = 0;
  Rig<MinimalPort> r(c);
  CHECK_EQ(r.sensor.EnsureInitialized().error, Error::InvalidArgument);
  CHECK_EQ(r.port.bus_up_calls, 0);
}

TEST_CASE("A port that cannot bring the bus up is Bus") {
  Rig<MinimalPort> r;
  r.Bus().bus_up_ok = false;
  CHECK_EQ(r.sensor.EnsureInitialized().error, Error::Bus);
  CHECK_EQ(r.sensor.Deinitialize().error, Error::Bus);
  CHECK_EQ(r.Bus().transfers, 0);
}

TEST_CASE("A wrong CHIP_ID is DeviceIdMismatch") {
  Rig<MinimalPort> r;
  r.Chip().chip_id = 0x51;
  CHECK_EQ(r.sensor.EnsureInitialized().error, Error::DeviceIdMismatch);
  CHECK(!r.sensor.IsInitialized());
  Rig<MinimalSpiPort> zero;
  zero.Chip().chip_id = 0x00;
  CHECK_EQ(zero.sensor.EnsureInitialized().error, Error::DeviceIdMismatch);
}

TEST_CASE("NVM not ready is Timeout after the configured deadline, never earlier") {
  Config c{};
  c.startup_timeout_ms = 3;
  Rig<MinimalPort> r(c);
  r.Chip().nvm_never_ready = true;
  CHECK_EQ(r.sensor.EnsureInitialized().error, Error::Timeout);
  const uint64_t waited =
      r.port.delayed_us - chip::kPowerUpMaxUs - chip::kStandbyMaxUs - chip::kSoftResetMaxUs;
  CHECK(waited >= 3000U);
  CHECK(waited <= 4000U);
}

TEST_CASE("An NVM error at start-up is DeviceFault") {
  Rig<MinimalPort> r;
  r.Chip().nvm_error = true;
  CHECK_EQ(r.sensor.EnsureInitialized().error, Error::DeviceFault);
  Rig<MinimalPort> c;
  c.Chip().nvm_cmd_error = true;
  CHECK_EQ(c.sensor.EnsureInitialized().error, Error::DeviceFault);
}

TEST_CASE("Start-up needs INT_STATUS = 0x10 then 0x00: a reset that did not happen is refused") {
  // The SPI soft reset frame dropped, after the por of the power-up was read: no reset, no por.
  Rig<MinimalSpiPort> r;
  REQUIRE(r.Start());
  REQUIRE(r.sensor.Deinitialize().ok());
  r.Bus().drop_transfer = r.Next() + 2; // the selection read, ODR_CONFIG, then CMD
  CHECK_EQ(r.sensor.EnsureInitialized().error, Error::DeviceIdMismatch);
  CHECK(r.Start());
  // The second INT_STATUS read corrupt: it did not clear.
  Rig<MinimalPort> c;
  REQUIRE(c.Start());
  REQUIRE(c.sensor.Deinitialize().ok());
  // ODR_CONFIG, CMD, CHIP_ID, STATUS, INT_STATUS, INT_STATUS: index 5 from the start.
  c.Bus().corrupt_read = c.Next() + 5;
  c.Bus().corrupt_mask = 0x02;
  CHECK_EQ(c.sensor.EnsureInitialized().error, Error::DeviceIdMismatch);
  CHECK(c.Start());
}

// --- Configuration ------------------------------------------------------------------------------

TEST_CASE("ApplyConfig() before the start only stores the configuration") {
  Rig<MinimalPort> r;
  CHECK(r.sensor.ApplyConfig(bmp581::FifoPreset()).ok());
  CHECK(r.sensor.GetConfig() == bmp581::FifoPreset());
  CHECK_EQ(r.Bus().transfers, 0);
  Config bad{};
  bad.fifo_threshold = 16;
  bad.fifo_frame = FifoFrame::PressureAndTemperature;
  CHECK_EQ(r.sensor.ApplyConfig(bad).error, Error::InvalidArgument);
  CHECK(r.sensor.GetConfig() == bmp581::FifoPreset());
}

template <typename P>
void AppliesEveryRegister() {
  Rig<P> r;
  REQUIRE(r.Start());
  Config c{};
  c.pressure_oversampling = bmp581::Oversampling::X32;
  c.temperature_oversampling = bmp581::Oversampling::X2;
  c.odr = bmp581::OutputDataRate::Hz10;
  c.deep_standby = false;
  c.pressure_iir = bmp581::IirFilter::Coef15;
  c.temperature_iir = bmp581::IirFilter::Coef3;
  c.iir_to_data_registers = true;
  c.iir_to_fifo = true;
  c.iir_to_out_of_range = true;
  c.iir_flush_in_forced = true;
  c.fifo_frame = FifoFrame::Pressure;
  c.fifo_mode = bmp581::FifoMode::StopOnFull;
  c.fifo_threshold = 20;
  c.fifo_decimation = 2;
  c.int_pin_enable = true;
  c.int_mode = bmp581::IntMode::Pulsed;
  c.int_polarity = bmp581::IntPolarity::ActiveHigh;
  c.int_drive = bmp581::IntDrive::PushPull;
  c.int_pad_drive = 5;
  c.source_data_ready = true;
  c.source_fifo_full = true;
  c.source_fifo_threshold = true;
  c.source_out_of_range = true;
  c.oor_reference_pa = 97'150;
  c.oor_window_pa = 50;
  c.oor_count = bmp581::OorCount::Samples7;
  REQUIRE(r.sensor.ApplyConfig(c).ok());
  CHECK(r.sensor.IsConfigKnown());
  CHECK_EQ(r.Chip().Reg(chip::kOsrConfig), 0x69U); // press_en | osr_p 5 | osr_t 1
  CHECK_EQ(r.Chip().Reg(chip::kOdrConfig), 0xDCU); // deep_dis | odr 0x17
  CHECK_EQ(r.Chip().Reg(chip::kDspIir), 0x22U);    // set_iir_p 4 | set_iir_t 2
  CHECK_EQ(r.Chip().Reg(chip::kDspConfig), 0xFFU);
  CHECK_EQ(r.Chip().Reg(chip::kFifoSel), 0x0AU);
  CHECK_EQ(r.Chip().Reg(chip::kFifoConfig), 0x34U);
  CHECK_EQ(r.Chip().Reg(chip::kIntConfig), 0x5AU);
  CHECK_EQ(r.Chip().Reg(chip::kIntSource), 0x0FU);
  // The example of section 4.7.2.3.
  CHECK_EQ(r.Chip().Reg(chip::kOorThrPLsb), 0x7EU);
  CHECK_EQ(r.Chip().Reg(chip::kOorThrPMsb), 0x7BU);
  CHECK_EQ(r.Chip().Reg(chip::kOorRange), 0x32U);
  CHECK_EQ(r.Chip().Reg(chip::kOorConfig), 0x81U);
  CHECK(r.sensor.GetConfig() == c);
}

TEST_CASE("ApplyConfig() writes every field where section 7 puts it, on I2C and SPI") {
  AppliesEveryRegister<MinimalPort>();
  AppliesEveryRegister<MinimalSpiPort>();
}

TEST_CASE("An INT mode change clears INT_SOURCE and reads INT_STATUS first (section 4.7.3.2)") {
  Rig<MinimalPort> r;
  REQUIRE(r.Start());
  r.Bus().log.clear();
  Config c{};
  c.int_mode = bmp581::IntMode::Pulsed;
  REQUIRE(r.sensor.ApplyConfig(c).ok());
  // log: INT_STATUS and the sentinel (the reset check), ODR_CONFIG out of deep standby, NVM_DATA
  // (the marker), INT_SOURCE = 0, INT_STATUS, INT_CONFIG, ...
  REQUIRE(r.Bus().log.size() > 7U);
  CHECK(!r.Bus().log[0].write && (r.Bus().log[0].reg == chip::kIntStatus));
  CHECK(!r.Bus().log[1].write && (r.Bus().log[1].reg == chip::kOsrConfig));
  CHECK(r.Bus().log[2].write && (r.Bus().log[2].reg == chip::kOdrConfig));
  CHECK(!r.Bus().log[3].write && (r.Bus().log[3].reg == chip::kNvmDataLsb));
  REQUIRE(r.Bus().log[4].write);
  CHECK_EQ(r.Bus().log[4].reg, chip::kIntSource);
  REQUIRE(!r.Bus().log[4].bytes.empty());
  CHECK_EQ(r.Bus().log[4].bytes[0], 0U);
  CHECK(!r.Bus().log[5].write && (r.Bus().log[5].reg == chip::kIntStatus));
  CHECK(r.Bus().log[6].write && (r.Bus().log[6].reg == chip::kIntConfig));
  CHECK_EQ(r.Bus().WritesTo(chip::kIntSource), 2);
}

TEST_CASE("ApplyConfig() while measuring goes to standby first and waits t_standby") {
  Rig<MinimalPort> r(bmp581::LowPowerPreset());
  REQUIRE(r.Start());
  CHECK_EQ(r.Chip().Mode(), ModelMode::Normal);
  const uint64_t before = r.port.delayed_us;
  Config c = bmp581::LowPowerPreset();
  c.odr = bmp581::OutputDataRate::Hz2;
  REQUIRE(r.sensor.ApplyConfig(c).ok());
  CHECK(r.port.delayed_us - before >= chip::kStandbyMaxUs);
  CHECK_EQ(r.Chip().Mode(), ModelMode::Normal);
  CHECK_EQ(static_cast<uint32_t>(r.Chip().Reg(chip::kOdrConfig)) >> 2U, 0x1BU);
}

TEST_CASE("A standby that never comes is Timeout and leaves the configuration in doubt") {
  Rig<MinimalPort> r(bmp581::LowPowerPreset());
  REQUIRE(r.Start());
  r.Chip().standby_never = true;
  CHECK_EQ(r.sensor.ApplyConfig(Config{}).error, Error::Timeout);
  CHECK(!r.sensor.IsConfigKnown());
  CHECK_EQ(r.sensor.SetPowerMode(PowerMode::Standby).error, Error::Timeout);
  CHECK_EQ(r.sensor.MeasureOnce().error, Error::StateUnknown);
}

TEST_CASE("Both readings of the mode read-back work: at once, or after a lag") {
  for (const uint32_t lag : {0U, 1500U, 3500U}) {
    Rig<MinimalPort> r(bmp581::LowPowerPreset());
    r.Chip().mode_lag_us = lag;
    REQUIRE(r.Start());
    CHECK_EQ(r.Chip().Mode(), ModelMode::Normal);
    CHECK(r.sensor.SetPowerMode(PowerMode::Continuous).ok());
  }
  // A mode that never shows is WriteMismatch, after the bounded wait.
  Rig<MinimalPort> never(bmp581::LowPowerPreset());
  never.Chip().mode_lag_us = 1'000'000;
  CHECK_EQ(never.sensor.EnsureInitialized().error, Error::WriteMismatch);
}

TEST_CASE("EnsureInitialized() on a running driver whose configuration is in doubt starts again") {
  Rig<MinimalPort> r;
  REQUIRE(r.Start());
  r.Bus().lost_ack_transfer = r.Next() + 4; // a configuration write of ApplyConfig
  CHECK(!r.sensor.ApplyConfig(bmp581::HighResolutionPreset()).ok());
  REQUIRE(!r.sensor.IsConfigKnown());
  CHECK(r.Start());
  CHECK(r.sensor.IsConfigKnown());
  CHECK_EQ(r.Chip().soft_resets, 2);
}

TEST_CASE("Both readings of the standby read-back work: at once, or after the transition") {
  for (const uint32_t delay : {0U, 1000U, 2400U}) {
    Rig<MinimalPort> r(bmp581::LowPowerPreset());
    r.Chip().standby_delay_us = delay;
    REQUIRE(r.Start());
    CHECK(r.sensor.SetPowerMode(PowerMode::Standby).ok());
    CHECK_EQ(r.Chip().Mode(), ModelMode::Standby);
    CHECK(r.sensor.ApplyConfig(bmp581::HighResolutionPreset()).ok());
  }
}

// --- Power modes --------------------------------------------------------------------------------

TEST_CASE("SetPowerMode() goes through standby and reads the mode back") {
  Rig<MinimalPort> r;
  REQUIRE(r.Start());
  REQUIRE(r.sensor.SetPowerMode(PowerMode::Normal).ok());
  CHECK_EQ(r.Chip().Mode(), ModelMode::Normal);
  CHECK_EQ(r.sensor.GetConfig().mode, PowerMode::Normal);
  CHECK((r.sensor.GetLastOsrEff() & 0x80U) != 0U);
  const int odr_writes = r.Bus().WritesTo(chip::kOdrConfig);
  REQUIRE(r.sensor.SetPowerMode(PowerMode::Continuous).ok());
  CHECK_EQ(r.Bus().WritesTo(chip::kOdrConfig) - odr_writes, 2); // standby, then continuous
  CHECK_EQ(r.Chip().Mode(), ModelMode::Continuous);
  REQUIRE(r.sensor.SetPowerMode(PowerMode::Standby).ok());
  CHECK_EQ(r.Chip().Mode(), ModelMode::Standby);
}

TEST_CASE("SetPowerMode() refuses Forced, a non-enumerator and a rate normal mode cannot reach") {
  Config c{};
  c.pressure_oversampling = bmp581::Oversampling::X128;
  c.odr = bmp581::OutputDataRate::Hz20;
  Rig<MinimalPort> r(c);
  REQUIRE(r.Start());
  const int before = r.Next();
  CHECK_EQ(r.sensor.SetPowerMode(PowerMode::Forced).error, Error::InvalidArgument);
  CHECK_EQ(r.sensor.SetPowerMode(NotAnEnumerator<PowerMode>(9)).error, Error::InvalidArgument);
  CHECK_EQ(r.sensor.SetPowerMode(PowerMode::Normal).error, Error::InvalidArgument);
  CHECK_EQ(r.Next(), before);
  CHECK(r.sensor.SetPowerMode(PowerMode::Continuous).ok()); // no data rate in continuous mode
}

TEST_CASE("Normal mode that the chip rejects is ConfigRejected, and the chip goes to standby") {
  Rig<MinimalPort> r(bmp581::LowPowerPreset());
  r.Chip().reject_odr = true;
  CHECK_EQ(r.sensor.EnsureInitialized().error, Error::ConfigRejected);
  CHECK(!r.sensor.IsInitialized());
  CHECK_EQ(r.Chip().Mode(), ModelMode::Standby);
}

TEST_CASE("Deep standby: a forced measurement waits the longer start-up and still completes") {
  Rig<MinimalPort> deep;
  REQUIRE(deep.Start());
  CHECK(bmp581::EntersDeepStandby(deep.sensor.GetConfig()));
  CHECK(deep.sensor.MeasureOnce().ok());
  Config c{};
  c.deep_standby = false;
  Rig<MinimalPort> shallow(c);
  REQUIRE(shallow.Start());
  CHECK(shallow.sensor.MeasureOnce().ok());
  CHECK(bmp581::ForcedMeasurementMaxUs(Config{}) > bmp581::ForcedMeasurementMaxUs(c));
}

// --- Measurement --------------------------------------------------------------------------------

template <typename P>
void ConvertsExactly() {
  Rig<P> r;
  REQUIRE(r.Start());
  r.Chip().pressure_raw = 6'484'832;   // 101325.5 Pa
  r.Chip().temperature_raw = -802'816; // -12.25 °C
  const auto m = r.sensor.MeasureOnce();
  REQUIRE(m.ok());
  CHECK_EQ(m.value.pressure_raw, 6'484'832);
  CHECK_EQ(m.value.temperature_raw, -802'816);
  CHECK(bmp581::PressurePa(m.value.pressure_raw) == 101'325.5F);
  CHECK(bmp581::TemperatureC(m.value.temperature_raw) == -12.25F);
  // The same frame read again, from the data registers.
  const auto again = r.sensor.ReadMeasurement();
  REQUIRE(again.ok());
  CHECK_EQ(again.value.pressure_raw, 6'484'832);
}

TEST_CASE("MeasureOnce() returns the chip's values exactly, on I2C and SPI") {
  ConvertsExactly<MinimalPort>();
  ConvertsExactly<MinimalSpiPort>();
}

TEST_CASE("The 24-bit registers convert exactly: 1/64 Pa and 1/65536 °C (section 4.5)") {
  CHECK(bmp581::PressurePa(1) == 0.015625F);
  CHECK(bmp581::PressurePa(RawPa(125'000)) == 125'000.0F);
  CHECK(bmp581::PressurePa(8'000'001) == 125'000.015625F);
  CHECK(bmp581::TemperatureC(1) == 0.0000152587890625F);
  CHECK(bmp581::TemperatureC(RawC(85)) == 85.0F);
  CHECK(bmp581::TemperatureC(-2'621'439) == -39.9999847412109375F);
  CHECK_EQ(bmp581::SignExtend24(0xF6'0000U), -655'360);   // -10 °C
  CHECK_EQ(bmp581::SignExtend24(0xFFF6'0000U), -655'360); // bits 31..24 ignored
  std::array<uint8_t, 3> bytes{0x00U, 0x00U, 0xF6U};
  CHECK_EQ(bmp581::detail::Le24(bytes), 0xF6'0000U);
}

TEST_CASE("The measurement range is checked, both ends included, never clamped") {
  struct Case {
    int32_t pressure;
    int32_t temperature;
    Error expected;
  };
  const std::array<Case, 8> cases{{
      {RawPa(30'000), RawC(25), Error::Ok},
      {RawPa(30'000) - 1, RawC(25), Error::OutOfRange},
      {RawPa(125'000), RawC(25), Error::Ok},
      {RawPa(125'000) + 1, RawC(25), Error::OutOfRange},
      {RawPa(100'000), RawC(-40), Error::Ok},
      {RawPa(100'000), RawC(-40) - 1, Error::OutOfRange},
      {RawPa(100'000), RawC(85), Error::Ok},
      {RawPa(100'000), RawC(85) + 1, Error::OutOfRange},
  }};
  for (const Case& c : cases) {
    Rig<MinimalPort> r;
    REQUIRE(r.Start());
    r.Chip().pressure_raw = c.pressure;
    r.Chip().temperature_raw = c.temperature;
    const auto m = r.sensor.MeasureOnce();
    CHECK_EQ(m.error, c.expected);
    if (!m.ok()) {
      CHECK_EQ(m.value.pressure_raw, 0);
    }
  }
}

TEST_CASE("Data registers at their reset value are NoData, never a measurement") {
  Rig<MinimalPort> r;
  REQUIRE(r.Start());
  CHECK_EQ(r.sensor.ReadMeasurement().error, Error::NoData);
  CHECK(r.sensor.IsInitialized());
  CHECK(r.sensor.MeasureOnce().ok());
}

TEST_CASE("Verify::ReadBack adds the reset check to a measurement read") {
  Rig<MinimalPort> r;
  REQUIRE(r.Start());
  r.Chip().temperature_raw = RawC(25) + 0x5D58;
  REQUIRE(r.sensor.MeasureOnce().ok());
  const int before = r.Bus().ReadsFrom(chip::kOsrConfig);
  // The check reads after the data: the values must still be the data's (a shared buffer once
  // let the check's bytes into the temperature).
  const auto checked = r.sensor.ReadMeasurement(Verify::ReadBack);
  CHECK(checked.ok());
  CHECK_EQ(checked.value.temperature_raw, r.Chip().temperature_raw);
  CHECK_EQ(checked.value.pressure_raw, r.Chip().pressure_raw);
  CHECK_EQ(r.Bus().ReadsFrom(chip::kOsrConfig) - before, 1);
  CHECK(r.sensor.ReadMeasurement().ok());
  CHECK_EQ(r.Bus().ReadsFrom(chip::kOsrConfig) - before, 1);
  CHECK_EQ(r.sensor.ReadMeasurement(NotAnEnumerator<Verify>(7)).error, Error::InvalidArgument);
}

TEST_CASE("MeasureOnce() needs standby: Busy otherwise, with nothing sent") {
  Rig<MinimalPort> r(bmp581::LowPowerPreset());
  REQUIRE(r.Start());
  const int before = r.Next();
  CHECK_EQ(r.sensor.MeasureOnce().error, Error::Busy);
  CHECK_EQ(r.Next(), before);
}

TEST_CASE("MeasureOnce() while a forced measurement still runs is Busy") {
  Rig<MinimalPort> r;
  REQUIRE(r.Start());
  r.Chip().forced_never_ends = true;
  CHECK_EQ(r.sensor.MeasureOnce().error, Error::Timeout);
  CHECK_EQ(r.sensor.MeasureOnce().error, Error::Busy);
  // Standby is never refused, and ends it.
  CHECK(r.sensor.SetPowerMode(PowerMode::Standby).ok());
  r.Chip().forced_never_ends = false;
  CHECK(r.sensor.MeasureOnce().ok());
}

TEST_CASE("A forced measurement that does not end is Timeout within the datasheet maximum") {
  Config c = bmp581::ForcedPreset();
  c.pressure_oversampling = bmp581::Oversampling::X128;
  c.temperature_oversampling = bmp581::Oversampling::X8;
  Rig<MinimalPort> r(c);
  REQUIRE(r.Start());
  r.Chip().forced_never_ends = true;
  const uint64_t before = r.port.delayed_us;
  CHECK_EQ(r.sensor.MeasureOnce().error, Error::Timeout);
  const uint64_t waited = r.port.delayed_us - before;
  CHECK(waited >= bmp581::ForcedMeasurementMaxUs(c));
  CHECK(waited <= (bmp581::ForcedMeasurementMaxUs(c) + (2U * bmp581::kPollUs)));
}

TEST_CASE("Every oversampling pair measures within the forced-mode deadline") {
  for (uint8_t p = 0; p < 8U; ++p) {
    for (uint8_t t = 0; t < 8U; ++t) {
      Config c{};
      c.pressure_oversampling = static_cast<bmp581::Oversampling>(p);
      c.temperature_oversampling = static_cast<bmp581::Oversampling>(t);
      Rig<MinimalPort> r(c);
      REQUIRE(r.Start());
      CHECK(r.sensor.MeasureOnce().ok());
    }
  }
}

template <typename P>
void NormalModeDataReady() {
  Config c = bmp581::LowPowerPreset();
  c.odr = bmp581::OutputDataRate::Hz10;
  Rig<P> r(c);
  REQUIRE(r.Start());
  r.Chip().pressure_raw = RawPa(95'000);
  REQUIRE(r.sensor.WaitDataReady().ok());
  CHECK(r.sensor.GetLatchedInterrupts().data_ready);
  const auto m = r.sensor.ReadMeasurement();
  REQUIRE(m.ok());
  CHECK_EQ(m.value.pressure_raw, RawPa(95'000));
  // Next period.
  CHECK(r.sensor.WaitDataReady().ok());
  CHECK(r.Chip().measurements >= 2);
}

TEST_CASE("Normal mode: WaitDataReady() then ReadMeasurement(), on I2C and SPI") {
  NormalModeDataReady<MinimalPort>();
  NormalModeDataReady<MinimalSpiPort>();
}

TEST_CASE("Continuous mode measures back to back") {
  Config c{};
  c.mode = PowerMode::Continuous;
  c.source_data_ready = true;
  Rig<MinimalPort> r(c);
  REQUIRE(r.Start());
  CHECK(r.sensor.WaitDataReady().ok());
  CHECK(r.sensor.ReadMeasurement().ok());
  r.Chip().Elapse(20'000);
  CHECK(r.Chip().measurements >= 5);
}

TEST_CASE("WaitDataReady() needs the data-ready source and a measuring mode") {
  Rig<MinimalPort> r;
  REQUIRE(r.Start());
  CHECK_EQ(r.sensor.WaitDataReady().error, Error::Unsupported);
  Config c{};
  c.mode = PowerMode::Normal;
  Rig<MinimalPort> no_source(c);
  REQUIRE(no_source.Start());
  CHECK_EQ(no_source.sensor.WaitDataReady().error, Error::Unsupported);
}

TEST_CASE("WaitDataReady() gives up after Config::data_ready_timeout_ms") {
  Config c = bmp581::LowPowerPreset();
  c.odr = bmp581::OutputDataRate::Hz0p125;
  c.data_ready_timeout_ms = 2;
  Rig<MinimalPort> r(c);
  REQUIRE(r.Start());
  const uint64_t before = r.port.delayed_us;
  CHECK_EQ(r.sensor.WaitDataReady().error, Error::Timeout);
  CHECK(r.port.delayed_us - before >= 2000U);
  CHECK(r.port.delayed_us - before <= 2000U + bmp581::kPollUs);
}

// --- Interrupts ---------------------------------------------------------------------------------

TEST_CASE("ReadInterruptStatus() decodes INT_STATUS and every read feeds the latch") {
  Config c = bmp581::LowPowerPreset();
  c.source_out_of_range = true;
  c.oor_reference_pa = 50'000;
  c.oor_window_pa = 10;
  Rig<MinimalPort> r(c);
  REQUIRE(r.Start());
  REQUIRE(r.sensor.WaitDataReady().ok());
  const auto s = r.sensor.ReadInterruptStatus();
  REQUIRE(s.ok());
  CHECK(!s.value.data_ready); // cleared by the WaitDataReady() read
  CHECK(r.sensor.GetLatchedInterrupts().data_ready);
  CHECK(r.sensor.GetLatchedInterrupts().out_of_range); // 100 kPa is outside 50 kPa +/- 10 Pa
  r.sensor.ClearLatchedInterrupts();
  CHECK(!r.sensor.GetLatchedInterrupts().data_ready);
  CHECK(!r.sensor.GetLatchedInterrupts().out_of_range);
}

TEST_CASE("Both readings of INT_STATUS for disabled sources work") {
  Rig<MinimalPort> r(bmp581::LowPowerPreset());
  r.Chip().flags_for_disabled_sources = true;
  REQUIRE(r.Start());
  CHECK(r.sensor.WaitDataReady().ok());
  CHECK(r.sensor.ReadMeasurement().ok());
}

// --- FIFO ---------------------------------------------------------------------------------------

template <typename P>
void ReadsPtFifo() {
  Rig<P> r(bmp581::FifoPreset());
  REQUIRE(r.Start());
  r.Chip().pressure_raw = RawPa(99'000) + 3;
  r.Chip().temperature_raw = RawC(21) + 7;
  r.Chip().Elapse(500'000); // 10 Hz: about 5 frames
  const auto count = r.sensor.ReadFifoCount();
  REQUIRE(count.ok());
  CHECK(count.value >= 4U);
  std::array<FifoSample, 16> out{};
  const auto n = r.sensor.ReadFifo(out);
  REQUIRE(n.ok());
  CHECK_EQ(n.value, static_cast<std::size_t>(count.value));
  for (std::size_t i = 0; i < n.value; ++i) {
    CHECK_EQ(out[i].error, Error::Ok);
    CHECK(out[i].has_pressure && out[i].has_temperature);
    CHECK_EQ(out[i].pressure_raw, RawPa(99'000) + 3);
    CHECK_EQ(out[i].temperature_raw, RawC(21) + 7);
  }
  CHECK_EQ(r.Chip().FifoFrames(), 0U);
}

TEST_CASE("The FIFO returns whole pressure-and-temperature frames, on I2C and SPI") {
  ReadsPtFifo<MinimalPort>();
  ReadsPtFifo<MinimalSpiPort>();
}

TEST_CASE("Pressure-only and temperature-only FIFO frames") {
  for (const FifoFrame f : {FifoFrame::Pressure, FifoFrame::Temperature}) {
    Config c = bmp581::FifoPreset();
    c.fifo_frame = f;
    c.fifo_threshold = 20;
    Rig<MinimalPort> r(c);
    REQUIRE(r.Start());
    r.Chip().Elapse(400'000);
    std::array<FifoSample, 32> out{};
    const auto n = r.sensor.ReadFifo(out);
    REQUIRE(n.ok());
    REQUIRE(n.value > 0U);
    CHECK_EQ(out[0].has_pressure, f == FifoFrame::Pressure);
    CHECK_EQ(out[0].has_temperature, f == FifoFrame::Temperature);
    if (f == FifoFrame::Pressure) {
      CHECK_EQ(out[0].pressure_raw, r.Chip().pressure_raw);
    } else {
      CHECK_EQ(out[0].temperature_raw, r.Chip().temperature_raw);
    }
  }
}

TEST_CASE("A FIFO read takes at most out.size() frames; the rest stay") {
  Rig<MinimalPort> r(bmp581::FifoPreset());
  REQUIRE(r.Start());
  r.Chip().Elapse(600'000);
  const std::size_t frames = r.Chip().FifoFrames();
  REQUIRE(frames >= 3U);
  std::array<FifoSample, 2> out{};
  const auto n = r.sensor.ReadFifo(out);
  REQUIRE(n.ok());
  CHECK_EQ(n.value, 2U);
  CHECK(r.Chip().FifoFrames() >= frames - 2U);
}

TEST_CASE("A full FIFO streams or stops, and the flags reach the latch") {
  for (const bmp581::FifoMode mode : {bmp581::FifoMode::Streaming, bmp581::FifoMode::StopOnFull}) {
    Config c = bmp581::FifoPreset();
    c.fifo_mode = mode;
    c.source_fifo_full = true;
    Rig<MinimalPort> r(c);
    REQUIRE(r.Start());
    r.Chip().Elapse(3'000'000);
    const auto count = r.sensor.ReadFifoCount();
    REQUIRE(count.ok());
    CHECK_EQ(count.value, 16U);
    const auto s = r.sensor.ReadInterruptStatus();
    REQUIRE(s.ok());
    CHECK(s.value.fifo_full && s.value.fifo_threshold);
  }
}

TEST_CASE("FIFO decimation keeps every 2^n-th frame") {
  Config c = bmp581::FifoPreset();
  c.fifo_decimation = 2;
  Rig<MinimalPort> r(c);
  REQUIRE(r.Start());
  const int before = r.Chip().measurements;
  r.Chip().Elapse(1'000'000);
  const int made = r.Chip().measurements - before;
  const auto count = r.sensor.ReadFifoCount();
  REQUIRE(count.ok());
  CHECK(static_cast<int>(count.value) <= ((made + 3) / 4));
}

TEST_CASE("FIFO frames at the empty value or out of range carry their own error") {
  Rig<MinimalPort> r(bmp581::FifoPreset());
  REQUIRE(r.Start());
  r.Chip().temperature_raw = 0x7F'7F7F;
  r.Chip().Elapse(250'000);
  std::array<FifoSample, 16> out{};
  auto n = r.sensor.ReadFifo(out);
  REQUIRE(n.ok());
  REQUIRE(n.value > 0U);
  CHECK_EQ(out[0].error, Error::NoData);
  CHECK(!out[0].has_pressure);
  r.Chip().temperature_raw = RawC(90);
  r.Chip().Elapse(250'000);
  n = r.sensor.ReadFifo(out);
  REQUIRE(n.ok());
  REQUIRE(n.value > 0U);
  CHECK_EQ(out[0].error, Error::OutOfRange);
}

TEST_CASE("ReadFifo() refuses an empty span, a FIFO that is off, and checks an empty FIFO") {
  Rig<MinimalPort> off;
  REQUIRE(off.Start());
  std::array<FifoSample, 4> out{};
  CHECK_EQ(off.sensor.ReadFifo(out).error, Error::Unsupported);
  CHECK_EQ(off.sensor.ReadFifo(std::span<FifoSample>{}).error, Error::InvalidArgument);
  CHECK_EQ(off.sensor.ReadFifo(out, NotAnEnumerator<Verify>(3)).error, Error::InvalidArgument);
  const auto zero = off.sensor.ReadFifoCount();
  CHECK(zero.ok() && (zero.value == 0U));
  Rig<MinimalPort> r(bmp581::FifoPreset());
  REQUIRE(r.Start());
  const int before = r.Bus().ReadsFrom(chip::kOsrConfig);
  const auto n = r.sensor.ReadFifo(out); // nothing measured yet
  REQUIRE(n.ok());
  CHECK_EQ(n.value, 0U);
  CHECK_EQ(r.Bus().ReadsFrom(chip::kOsrConfig) - before, 1); // the reset check
  r.Chip().Elapse(300'000);
  CHECK(r.sensor.ReadFifo(out, Verify::ReadBack).ok());
  CHECK_EQ(r.Bus().ReadsFrom(chip::kOsrConfig) - before, 2);
}

// --- Diagnostics --------------------------------------------------------------------------------

TEST_CASE("ReadRegister() reads once; FIFO_DATA, CMD and holes are refused with no I/O") {
  Rig<MinimalPort> r;
  REQUIRE(r.Start());
  const auto id = r.sensor.ReadRegister(reg::kChipId);
  CHECK(id.ok() && (id.value == chip::kChipIdValue));
  const int before = r.Next();
  CHECK_EQ(r.sensor.ReadRegister(reg::kFifoData).error, Error::InvalidArgument);
  CHECK_EQ(r.sensor.ReadRegister(reg::kCmd).error, Error::InvalidArgument);
  CHECK_EQ(r.sensor.ReadRegister(0x00).error, Error::InvalidArgument);
  CHECK_EQ(r.sensor.ReadRegister(0x12).error, Error::InvalidArgument);
  CHECK_EQ(r.Next(), before);
  const auto status = r.sensor.ReadRegister(reg::kStatus);
  CHECK(status.ok() && (r.sensor.GetLastStatus() == status.value));
  r.Chip().RaiseFlags(chip::kDrdy);
  r.Chip().SetReg(chip::kIntSource, chip::kDrdy);
  const auto ints = r.sensor.ReadRegister(reg::kIntStatus);
  CHECK(ints.ok() && (ints.value == chip::kDrdy));
  CHECK(r.sensor.GetLatchedInterrupts().data_ready);
  CHECK_EQ(r.sensor.GetLastIntStatus(), chip::kDrdy);
  CHECK(r.sensor.ReadRegister(reg::kOsrEff).ok());
}

// --- Deinitialize -------------------------------------------------------------------------------

template <typename P>
void DeinitialisesBySoftReset() {
  Rig<P> r(bmp581::LowPowerPreset());
  REQUIRE(r.Start());
  REQUIRE(r.sensor.Deinitialize().ok());
  CHECK(!r.sensor.IsInitialized());
  CHECK(!r.sensor.IsConfigKnown());
  CHECK_EQ(r.Chip().soft_resets, 2);
  CHECK_EQ(r.Chip().Mode(), ModelMode::Standby);
  CHECK_EQ(r.sensor.ReadMeasurement().error, Error::NotInitialized);
  CHECK(r.Start()); // starts again
}

TEST_CASE("Deinitialize() is a proven soft reset into deep standby, on I2C and SPI") {
  DeinitialisesBySoftReset<MinimalPort>();
  DeinitialisesBySoftReset<MinimalSpiPort>();
}

TEST_CASE("Deinitialize() works on a driver that never started") {
  Rig<MinimalPort> r;
  CHECK(r.sensor.Deinitialize().ok());
  CHECK_EQ(r.Chip().soft_resets, 1);
}

TEST_CASE("Deinitialize() is not Ok when the reset is not proved") {
  Rig<MinimalSpiPort> r;
  REQUIRE(r.Start());
  r.Bus().drop_transfer = r.Next() + 2; // the CMD frame after the selection read and ODR_CONFIG
  CHECK_EQ(r.sensor.Deinitialize().error, Error::WriteMismatch);
  CHECK(!r.sensor.IsInitialized());
  Rig<MinimalSpiPort> fails;
  REQUIRE(fails.Start());
  fails.Bus().fail_transfer = fails.Next() + 2; // CMD
  CHECK_EQ(fails.sensor.Deinitialize().error, Error::Bus);
  Rig<MinimalPort> read_fails;
  REQUIRE(read_fails.Start());
  read_fails.Bus().fail_transfer = read_fails.Next() + 2; // INT_STATUS after ODR_CONFIG, CMD
  CHECK_EQ(read_fails.sensor.Deinitialize().error, Error::Bus);
}

// --- Types --------------------------------------------------------------------------------------

TEST_CASE("Validate() follows the documented ranges and table 7") {
  CHECK_EQ(bmp581::Validate(Config{}), Error::Ok);
  Config c{};
  c.mode = PowerMode::Forced;
  CHECK_EQ(bmp581::Validate(c), Error::InvalidArgument);
  c = Config{};
  c.fifo_frame = FifoFrame::Pressure;
  c.fifo_threshold = 31;
  CHECK_EQ(bmp581::Validate(c), Error::Ok);
  c.fifo_threshold = 32;
  CHECK_EQ(bmp581::Validate(c), Error::InvalidArgument);
  c = Config{};
  c.fifo_decimation = 8;
  CHECK_EQ(bmp581::Validate(c), Error::InvalidArgument);
  c = Config{};
  c.int_pad_drive = 16;
  CHECK_EQ(bmp581::Validate(c), Error::InvalidArgument);
  c.int_pad_drive = 8; // bit 3 with open drain only (section 7.5)
  CHECK_EQ(bmp581::Validate(c), Error::Ok);
  c.int_drive = bmp581::IntDrive::PushPull;
  CHECK_EQ(bmp581::Validate(c), Error::InvalidArgument);
  c.int_pad_drive = 7;
  CHECK_EQ(bmp581::Validate(c), Error::Ok);
  c = Config{};
  c.oor_reference_pa = bmp581::kOorReferenceMaxPa + 1U;
  CHECK_EQ(bmp581::Validate(c), Error::InvalidArgument);
  c = Config{};
  c.data_ready_timeout_ms = bmp581::kTimeoutMaxMs + 1U;
  CHECK_EQ(bmp581::Validate(c), Error::InvalidArgument);
  c = Config{};
  c.startup_timeout_ms = bmp581::kTimeoutMaxMs + 1U;
  CHECK_EQ(bmp581::Validate(c), Error::InvalidArgument);
  c = Config{};
  c.oor_count = NotAnEnumerator<bmp581::OorCount>(4);
  CHECK_EQ(bmp581::Validate(c), Error::InvalidArgument);
  c = Config{};
  c.mode = PowerMode::Normal;
  c.odr = bmp581::OutputDataRate::Hz240;
  CHECK_EQ(bmp581::Validate(c), Error::Ok);
  c.temperature_oversampling = bmp581::Oversampling::X16; // table 7: 200 Hz
  CHECK_EQ(bmp581::Validate(c), Error::InvalidArgument);
  c.odr = bmp581::OutputDataRate::Hz199p1;
  CHECK_EQ(bmp581::Validate(c), Error::Ok);
}

TEST_CASE("Every data rate and every table 7 cell is what section 7.34 and table 7 say") {
  CHECK_EQ(bmp581::OdrMilliHz(bmp581::OutputDataRate::Hz218p5), 218'537U);
  CHECK_EQ(bmp581::OdrMilliHz(bmp581::OutputDataRate::Hz0p125), 125U);
  CHECK_EQ(bmp581::MaxOdrMilliHz(bmp581::Oversampling::X8, bmp581::Oversampling::X32), 80'000U);
  CHECK_EQ(bmp581::MaxOdrMilliHz(bmp581::Oversampling::X64, bmp581::Oversampling::X64), 15'000U);
  for (uint32_t code = 0; code < 32U; ++code) {
    CHECK_EQ(bmp581::OdrMilliHz(static_cast<bmp581::OutputDataRate>(code)),
             chip::kOdrMilliHz[code]);
  }
  for (uint32_t p = 0; p < 8U; ++p) {
    for (uint32_t t = 0; t < 8U; ++t) {
      CHECK_EQ(bmp581::MaxOdrMilliHz(static_cast<bmp581::Oversampling>(p),
                                     static_cast<bmp581::Oversampling>(t)),
               chip::kMaxOdrHz[p][t] * 1000U);
    }
  }
}

TEST_CASE("ToString() names every error") {
  CHECK(std::string_view{bmp581::ToString(Error::ConfigRejected)} == "ConfigRejected");
  CHECK(std::string_view{bmp581::ToString(NotAnEnumerator<Error>(200))} == "Unknown");
}

TEST_CASE("A CheckedConfig is proven valid at build time") {
  constexpr bmp581::CheckedConfig kBoard{bmp581::HighResolutionPreset()};
  Rig<MinimalPort> r(kBoard.Get());
  CHECK(r.Start());
  CHECK_EQ(r.Chip().Mode(), ModelMode::Normal);
}

TEST_CASE("A port with every optional hook logs failures") {
  Rig<FullSpiPort> r;
  r.Chip().chip_id = 0x60;
  CHECK_EQ(r.sensor.EnsureInitialized().error, Error::DeviceIdMismatch);
  CHECK(r.port.error_lines > 0);
}

} // namespace
