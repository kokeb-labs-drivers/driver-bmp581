/**
 * @file test_model.cpp
 * @brief The chip model's register map against the driver's, at compile time (handbook KB-TST-030),
 * and the model's own behaviour against the datasheet.
 *
 * tests/mock_bmp581.hpp is written from the datasheet without the driver's headers, so a constant
 * the driver gets wrong is not copied into the model. These checks make the two agree, or the build
 * fails: a difference is a mistake in one of them, found against the datasheet section the model
 * names.
 */
#include <array>
#include <cstdint>

#include "bmp581.hpp"
#include "mock_bmp581.hpp"
#include "test_harness.hpp"

namespace {

namespace chip = bmp581_test::chip;
namespace reg = bmp581::reg;
using bmp581_test::Bmp581Model;
using bmp581_test::ModelMode;

// --8<-- [start:cross-check]
// Register map (section 7).
static_assert(chip::kChipId == reg::kChipId && chip::kRevId == reg::kRevId &&
              chip::kChipStatus == reg::kChipStatus && chip::kDriveConfig == reg::kDriveConfig);
static_assert(chip::kIntConfig == reg::kIntConfig && chip::kIntSource == reg::kIntSource &&
              chip::kFifoConfig == reg::kFifoConfig && chip::kFifoCount == reg::kFifoCount &&
              chip::kFifoSel == reg::kFifoSel);
static_assert(chip::kTempXlsb == reg::kTempXlsb && chip::kPressXlsb == reg::kPressXlsb &&
              chip::kIntStatus == reg::kIntStatus && chip::kStatus == reg::kStatus &&
              chip::kFifoData == reg::kFifoData);
static_assert(chip::kNvmAddr == reg::kNvmAddr && chip::kNvmDataLsb == reg::kNvmDataLsb &&
              chip::kNvmDataMsb == reg::kNvmDataMsb && chip::kDspConfig == reg::kDspConfig &&
              chip::kDspIir == reg::kDspIir);
static_assert(chip::kOorThrPLsb == reg::kOorThrPLsb && chip::kOorThrPMsb == reg::kOorThrPMsb &&
              chip::kOorRange == reg::kOorRange && chip::kOorConfig == reg::kOorConfig &&
              chip::kOsrConfig == reg::kOsrConfig && chip::kOdrConfig == reg::kOdrConfig &&
              chip::kOsrEff == reg::kOsrEff && chip::kCmd == reg::kCmd);
// Reset values, identity and commands (sections 4.3.9, 7).
static_assert(chip::kChipIdValue == bmp581::kChipId &&
              chip::kIntConfigReset == reg::kIntConfigReset &&
              chip::kDspConfigReset == reg::kDspConfigReset &&
              chip::kOdrConfigReset == reg::kOdrConfigReset &&
              chip::kSoftResetCommand == reg::kCmdSoftReset);
static_assert(chip::kDataResetByte * 0x01'0101U == reg::kDataReset &&
              chip::kFifoEmptyByte == reg::kFifoEmpty);
// Fields (sections 7.21, 7.22, 7.33, 7.34).
static_assert(chip::kPor == reg::kIntPor && chip::kDrdy == reg::kIntDrdy &&
              chip::kFifoFull == reg::kIntFifoFull && chip::kFifoThs == reg::kIntFifoThs &&
              chip::kOorP == reg::kIntOorP);
static_assert(chip::kNvmRdy == reg::kStatusNvmRdy && chip::kNvmErr == reg::kStatusNvmErr &&
              chip::kNvmCmdErr == reg::kStatusNvmCmdErr && chip::kPressEn == reg::kOsrPressEn &&
              chip::kDeepDis == reg::kOdrDeepDis);
// Timing (table 4).
static_assert(chip::kSoftResetMaxUs == bmp581::kSoftResetUs &&
              chip::kPowerUpMaxUs == bmp581::kPowerUpUs &&
              chip::kStandbyMaxUs == bmp581::kStandbyUs &&
              chip::kStartupMaxUs == bmp581::kStartupUs &&
              chip::kStartupDeepMaxUs == bmp581::kStartupDeepUs);
static_assert(chip::kConvPUs == bmp581::detail::kConvPressureUs &&
              chip::kConvTUs == bmp581::detail::kConvTemperatureUs);
// --8<-- [end:cross-check]

} // namespace

TEST_CASE("The model's tables agree with the driver's, cell by cell") {
  for (uint32_t i = 0; i < 32U; ++i) {
    CHECK_EQ(chip::kOdrMilliHz[i], bmp581::detail::kOdrMilliHz[i]);
    // The period is 1e9 / mHz, rounded.
    CHECK_EQ(chip::kOdrPeriodUs[i],
             (1'000'000'000U + (chip::kOdrMilliHz[i] / 2U)) / chip::kOdrMilliHz[i]);
  }
  for (uint32_t p = 0; p < 8U; ++p) {
    for (uint32_t t = 0; t < 8U; ++t) {
      CHECK_EQ(chip::kMaxOdrHz[p][t], static_cast<uint32_t>(bmp581::detail::kMaxOdrHz[p][t]));
    }
  }
}

TEST_CASE("Model: reset values, por, clear-on-read INT_STATUS") {
  Bmp581Model m;
  std::array<uint8_t, 2> id{};
  m.ReadBurst(chip::kChipId, id.data(), id.size());
  CHECK_EQ(id[0], chip::kChipIdValue);
  CHECK_EQ(id[1], chip::kRevIdValue);
  uint8_t s = 0;
  m.ReadBurst(chip::kIntStatus, &s, 1);
  CHECK_EQ(s, chip::kPor);
  m.ReadBurst(chip::kIntStatus, &s, 1);
  CHECK_EQ(s, 0U);
  std::array<uint8_t, 6> data{};
  m.ReadBurst(chip::kTempXlsb, data.data(), data.size());
  for (const uint8_t b : data) {
    CHECK_EQ(b, chip::kDataResetByte);
  }
}

TEST_CASE("Model: a forced measurement completes in time and returns to standby") {
  Bmp581Model m;
  const uint8_t forced = 0x72; // 1 Hz, standby->forced
  (void)m.WriteBurst(chip::kOdrConfig, &forced, 1, 1, true);
  CHECK_EQ(m.Mode(), ModelMode::Forced);
  m.Elapse(1000);
  CHECK_EQ(m.Mode(), ModelMode::Forced);
  m.Elapse(10'000);
  CHECK_EQ(m.Mode(), ModelMode::Standby);
  CHECK_EQ(m.measurements, 1);
}

TEST_CASE("Model: standby-only registers ignore writes outside standby; soft reset NACKs on I2C") {
  Bmp581Model m;
  const uint8_t normal = 0x71;
  (void)m.WriteBurst(chip::kOdrConfig, &normal, 1, 1, true);
  const uint8_t sel = 0x03;
  (void)m.WriteBurst(chip::kFifoSel, &sel, 1, 1, true);
  CHECK_EQ(m.Reg(chip::kFifoSel), 0U);
  const uint8_t cmd = chip::kSoftResetCommand;
  CHECK_EQ(m.WriteBurst(chip::kCmd, &cmd, 1, 1, true), 0U);
  CHECK_EQ(m.soft_resets, 1);
  CHECK(m.Busy());
}

TEST_CASE("Model: SPI starts in I2C mode; the first long frame selects SPI and carries no data") {
  Bmp581Model m;
  m.deep_standby_drops_writes = false; // the chip is in deep standby after the reset
  std::array<uint8_t, 2> tx{0x81, 0x00};
  std::array<uint8_t, 2> rx{};
  m.SpiFrame(tx.data(), rx.data(), tx.size());
  CHECK(m.spi_selected);
  CHECK(rx[1] != chip::kChipIdValue);
  m.SpiFrame(tx.data(), rx.data(), tx.size());
  CHECK_EQ(rx[1], chip::kChipIdValue);
  std::array<uint8_t, 4> burst{chip::kOorRange, 0x11, chip::kOorThrPLsb, 0x22};
  m.SpiFrame(burst.data(), nullptr, burst.size()); // address and data pairs
  CHECK_EQ(m.Reg(chip::kOorRange), 0x11U);
  CHECK_EQ(m.Reg(chip::kOorThrPLsb), 0x22U);
}
