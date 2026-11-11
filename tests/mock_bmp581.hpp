/**
 * @file mock_bmp581.hpp
 * @brief A register model of the BMP581, written from the datasheet without the driver's headers
 * (handbook KB-TST-030), with a virtual clock: the chip changes state on its own.
 *
 * What the model does by itself, as the datasheet says the chip does:
 * - a power-on reset (`PowerOnReset()`) or CMD = 0xB6 loads every register with its reset value,
 *   sets INT_STATUS.por, empties the FIFO, returns the interface to I2C mode, and keeps the chip
 *   busy (no answer) for `reset_busy_us`, then STATUS.status_nvm_rdy after `nvm_boot_us`
 *   (sections 4.3.9, 4.3.10, 5.1, 7);
 * - measurements complete in time: forced mode returns to standby after the start-up and the
 *   conversion time, normal mode measures once per data rate period, continuous mode back to back;
 *   each updates the data registers, sets drdy, fills the FIFO and runs the out-of-range check
 *   (sections 4.3, 4.6, 4.7);
 * - a change to standby completes `standby_delay_us` after it is commanded, and ODR_CONFIG.pwr_mode
 *   reads the mode the chip is in (section 4.3.7); writes to the standby-only registers outside
 *   standby are lost (section 4.3.8);
 * - INT_STATUS clears on read; the FIFO flags follow their condition (sections 4.7.2, 7.21).
 *
 * The datasheet gives no time for a mode change to show in ODR_CONFIG or for OSR_EFF.odr_is_valid
 * to be updated: `mode_lag_us` delays a new measuring mode, forced included, and
 * `odr_valid_lag_us` delays odr_is_valid on its own after normal mode starts; 0 is the immediate
 * reading.
 *
 * Where the datasheet leaves two readings open, a knob offers both: `standby_delay_us` (0: the
 * mode reads standby at once), `flags_for_disabled_sources` (whether INT_STATUS shows a source
 * that INT_SOURCE does not enable), `deep_standby_drops_writes` (section 4.3.1 says standby takes
 * every write; whether deep standby takes any but ODR_CONFIG and CMD is not said),
 * `nvm_download_rewrites_data` (the NVM is downloaded at the start of every measurement from deep
 * standby, section 7.22; whether that reaches NVM_DATA, the NVM read port, is not said), and
 * `select_fill` (what MISO shows during the SPI frame that only selects SPI, section 5.1).
 */
#ifndef BMP581_MOCK_BMP581_HPP
#define BMP581_MOCK_BMP581_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace bmp581_test {

/// The register map as the model knows it (datasheet section 7), apart from the driver's.
namespace chip {
inline constexpr uint8_t kChipId = 0x01;      ///< CHIP_ID (7.1).
inline constexpr uint8_t kRevId = 0x02;       ///< REV_ID (7.2).
inline constexpr uint8_t kChipStatus = 0x11;  ///< CHIP_STATUS (7.3).
inline constexpr uint8_t kDriveConfig = 0x13; ///< DRIVE_CONFIG (7.4).
inline constexpr uint8_t kIntConfig = 0x14;   ///< INT_CONFIG (7.5).
inline constexpr uint8_t kIntSource = 0x15;   ///< INT_SOURCE (7.6).
inline constexpr uint8_t kFifoConfig = 0x16;  ///< FIFO_CONFIG (7.7).
inline constexpr uint8_t kFifoCount = 0x17;   ///< FIFO_COUNT (7.8).
inline constexpr uint8_t kFifoSel = 0x18;     ///< FIFO_SEL (7.9).
inline constexpr uint8_t kTempXlsb = 0x1D;    ///< TEMP_DATA_XLSB (7.11).
inline constexpr uint8_t kPressXlsb = 0x20;   ///< PRESS_DATA_XLSB (7.14).
inline constexpr uint8_t kIntStatus = 0x27;   ///< INT_STATUS (7.21).
inline constexpr uint8_t kStatus = 0x28;      ///< STATUS (7.22).
inline constexpr uint8_t kFifoData = 0x29;    ///< FIFO_DATA (7.23).
inline constexpr uint8_t kNvmAddr = 0x2B;     ///< NVM_ADDR (7.24).
inline constexpr uint8_t kNvmDataLsb = 0x2C;  ///< NVM_DATA_LSB (7.25).
inline constexpr uint8_t kNvmDataMsb = 0x2D;  ///< NVM_DATA_MSB (7.26).
inline constexpr uint8_t kDspConfig = 0x30;   ///< DSP_CONFIG (7.27).
inline constexpr uint8_t kDspIir = 0x31;      ///< DSP_IIR (7.28).
inline constexpr uint8_t kOorThrPLsb = 0x32;  ///< OOR_THR_P_LSB (7.29).
inline constexpr uint8_t kOorThrPMsb = 0x33;  ///< OOR_THR_P_MSB (7.30).
inline constexpr uint8_t kOorRange = 0x34;    ///< OOR_RANGE (7.31).
inline constexpr uint8_t kOorConfig = 0x35;   ///< OOR_CONFIG (7.32).
inline constexpr uint8_t kOsrConfig = 0x36;   ///< OSR_CONFIG (7.33).
inline constexpr uint8_t kOdrConfig = 0x37;   ///< ODR_CONFIG (7.34).
inline constexpr uint8_t kOsrEff = 0x38;      ///< OSR_EFF (7.35).
inline constexpr uint8_t kCmd = 0x7E;         ///< CMD (7.36).

inline constexpr uint8_t kChipIdValue = 0x50;      ///< CHIP_ID reset value.
inline constexpr uint8_t kRevIdValue = 0x32;       ///< REV_ID reset value.
inline constexpr uint8_t kDriveConfigReset = 0x30; ///< DRIVE_CONFIG reset value.
inline constexpr uint8_t kIntConfigReset = 0x35;   ///< INT_CONFIG reset value.
inline constexpr uint8_t kDspConfigReset = 0x03;   ///< DSP_CONFIG reset value.
inline constexpr uint8_t kOdrConfigReset = 0x70;   ///< ODR_CONFIG reset value.
inline constexpr uint8_t kDataResetByte = 0x7F;    ///< Every data register byte after reset.
inline constexpr uint8_t kFifoEmptyByte = 0x7F;    ///< FIFO_DATA when empty (4.6.3).
inline constexpr uint8_t kSoftResetCommand = 0xB6; ///< CMD soft reset (7.36).

inline constexpr uint8_t kPor = 0x10;       ///< INT_STATUS.por.
inline constexpr uint8_t kDrdy = 0x01;      ///< INT_STATUS/INT_SOURCE drdy.
inline constexpr uint8_t kFifoFull = 0x02;  ///< fifo_full.
inline constexpr uint8_t kFifoThs = 0x04;   ///< fifo_ths.
inline constexpr uint8_t kOorP = 0x08;      ///< oor_p.
inline constexpr uint8_t kNvmRdy = 0x02;    ///< STATUS.status_nvm_rdy.
inline constexpr uint8_t kNvmErr = 0x04;    ///< STATUS.status_nvm_err.
inline constexpr uint8_t kNvmCmdErr = 0x08; ///< STATUS.status_nvm_cmd_err.
inline constexpr uint8_t kPressEn = 0x40;   ///< OSR_CONFIG.press_en.
inline constexpr uint8_t kDeepDis = 0x80;   ///< ODR_CONFIG.deep_dis.

inline constexpr uint32_t kSoftResetMaxUs = 2000;   ///< t_soft_res (table 4).
inline constexpr uint32_t kPowerUpMaxUs = 2000;     ///< t_powup (table 4).
inline constexpr uint32_t kStandbyMaxUs = 2500;     ///< t_standby (table 4).
inline constexpr uint32_t kStartupMaxUs = 3000;     ///< t_startup (table 4).
inline constexpr uint32_t kStartupDeepMaxUs = 4000; ///< t_startup_deep (table 4).
/// t_conv_p typical, by osr_p (table 4).
inline constexpr std::array<uint32_t, 8> kConvPUs{1000,  1700,  2900,  5400,
                                                  10400, 20400, 40400, 80400};
/// t_conv_t typical, by osr_t (table 4).
inline constexpr std::array<uint32_t, 8> kConvTUs{1000, 1100, 1500, 2100, 3300, 5800, 10800, 20800};
/// ODR periods in microseconds, by ODR code (section 7.34: 1e9 / millihertz, rounded).
inline constexpr std::array<uint32_t, 32> kOdrPeriodUs{
    4167,  4576,   5022,   5580,   6250,   6696,   7143,    7701,    8333,    9077,   9970,
    11161, 12500,  14286,  16667,  19978,  22210,  25000,   28571,   33333,   39992,  50000,
    66667, 100000, 200000, 250000, 333333, 500000, 1000000, 2000000, 4000000, 8000000};
/// Table 7, max ODR in normal mode in Hz by [osr_p][osr_t].
inline constexpr std::array<std::array<uint32_t, 8>, 8> kMaxOdrHz{
    {{240, 240, 240, 240, 200, 130, 80, 40},
     {240, 240, 240, 220, 180, 120, 70, 40},
     {220, 220, 200, 180, 140, 100, 70, 40},
     {140, 140, 130, 120, 100, 80, 50, 35},
     {80, 80, 80, 70, 70, 50, 45, 30},
     {45, 45, 40, 40, 40, 35, 30, 20},
     {20, 20, 20, 20, 20, 20, 15, 15},
     {10, 10, 10, 10, 10, 10, 10, 5}}};
/// ODR codes in millihertz (section 7.34).
inline constexpr std::array<uint32_t, 32> kOdrMilliHz{
    240000, 218537, 199111, 179200, 160000, 149333, 140000, 129855, 120000, 110164, 100299,
    89600,  80000,  70000,  60000,  50056,  45025,  40000,  35000,  30000,  25005,  20000,
    15000,  10000,  5000,   4000,   3000,   2000,   1000,   500,    250,    125};
} // namespace chip

/// The chip's power mode, as ODR_CONFIG.pwr_mode reads.
enum class ModelMode : uint8_t { Standby = 0, Normal = 1, Forced = 2, Continuous = 3 };

/// The BMP581 register model.
class Bmp581Model {
public:
  /// Bus time per byte, so every frame advances the clock.
  static constexpr uint64_t kByteUs = 10;
  static constexpr uint64_t kNever = ~uint64_t{0};

  // The environment, in the chip's own units.
  int32_t pressure_raw = 100'000 * 64;  ///< 1/64 Pa.
  int32_t temperature_raw = 25 * 65536; ///< 1/65536 °C.

  // Knobs.
  uint32_t reset_busy_us = 1500; ///< No answer this long after a reset (< t_soft_res).
  uint32_t nvm_boot_us = 300;    ///< status_nvm_rdy this long after the busy time.
  bool nvm_never_ready = false;  ///< status_nvm_rdy stays 0.
  bool nvm_error = false;        ///< status_nvm_err set.
  bool nvm_cmd_error = false;    ///< status_nvm_cmd_err set.
  uint8_t chip_id = chip::kChipIdValue;
  uint32_t standby_delay_us = 1000; ///< Commanded standby completes after this (< t_standby).
  uint32_t startup_us = 2500;       ///< Standby to the start of a measurement (< t_startup).
  uint32_t startup_deep_us = 3500;  ///< Deep standby to it (< t_startup_deep).
  bool forced_never_ends = false;   ///< A forced measurement never completes.
  bool standby_never = false;       ///< A commanded standby never completes.
  bool reject_odr = false;          ///< OSR_EFF.odr_is_valid reads 0 whatever the setting.
  bool flags_for_disabled_sources = false; ///< INT_STATUS shows sources INT_SOURCE does not enable.
  bool ignore_writes = false;    ///< Writes are acknowledged and lost (a deaf register file).
  uint32_t mode_lag_us = 0;      ///< A new measuring mode reads back as standby, and OSR_EFF as not
                                 ///< valid, this long after the write (0: at once).
  uint32_t odr_valid_lag_us = 0; ///< OSR_EFF.odr_is_valid reads 0 this long after normal mode
                                 ///< starts, whatever pwr_mode shows (0: at once).
  bool deep_standby_drops_writes = true;   ///< In deep standby, writes other than ODR_CONFIG and
                                           ///< CMD are lost (the strict reading; false: kept).
  uint8_t select_fill = 0xA5;              ///< SPI: every byte of the frame that only selects SPI.
  bool nvm_download_rewrites_data = false; ///< A measurement from deep standby loads NVM_DATA with
                                           ///< `nvm_download_data` (an NVM word).
  std::array<uint8_t, 2> nvm_download_data{0x12, 0x34}; ///< What that download leaves there.

  // Observations for the tests and the fuzz invariants.
  uint64_t now_us = 0;
  int resets = 0;              ///< Power-on and soft resets.
  int soft_resets = 0;         ///< CMD = 0xB6 received.
  int measurements = 0;        ///< Measurements completed.
  bool por_since_soft = false; ///< A power-on reset happened and no soft reset since.
  int writes_over_reset = 0;   ///< Writes other than CMD and a standby ODR_CONFIG after a power-on
                               ///< reset, before the next soft reset.
  int writes_lost_deep = 0;    ///< Writes lost in deep standby (`deep_standby_drops_writes`).
  bool spi_selected = false;   ///< The interface is in SPI mode (section 5.1).

  Bmp581Model() noexcept {
    loadResetValues();
  }

  /// A power-on reset (supply dip below the POR level, section 4.7.2.4).
  void PowerOnReset() noexcept {
    reset();
    por_since_soft = true;
  }

  /// Let time pass; measurements and mode changes complete on their own.
  void Elapse(uint64_t us) noexcept {
    const uint64_t target = now_us + us;
    for (int guard = 0; guard < 100000; ++guard) {
      const uint64_t next = (standby_at_ < measure_at_) ? standby_at_ : measure_at_;
      if (next > target) {
        break;
      }
      now_us = next;
      fire();
    }
    now_us = target;
  }

  /// No answer during a reset.
  [[nodiscard]] bool Busy() const noexcept {
    return now_us < busy_until_;
  }

  /// The stored value of a register, without side effects.
  [[nodiscard]] uint8_t Reg(uint8_t a) const noexcept {
    return regs_[a & 0x7FU];
  }
  /// Set a register directly (a test arranging a state).
  void SetReg(uint8_t a, uint8_t v) noexcept {
    regs_[a & 0x7FU] = v;
  }
  /// The power mode the chip is in.
  [[nodiscard]] ModelMode Mode() const noexcept {
    return mode_;
  }
  /// Frames in the FIFO.
  [[nodiscard]] std::size_t FifoFrames() const noexcept {
    return fifo_.size();
  }
  /// INT_STATUS flags not yet read.
  [[nodiscard]] uint8_t PendingFlags() const noexcept {
    return flags_;
  }
  /// Set flags as if events happened (a test arranging INT_STATUS).
  void RaiseFlags(uint8_t f) noexcept {
    flags_ = static_cast<uint8_t>(flags_ | f);
  }

  /// A read burst from `start`: the address increments except at FIFO_DATA (section 5.4).
  void ReadBurst(uint8_t start, uint8_t* out, std::size_t n) noexcept {
    auto a = static_cast<uint8_t>(start & 0x7FU);
    std::size_t fifo_bytes = 0;
    bool int_status_read = false;
    for (std::size_t i = 0; i < n; ++i) {
      if (a == chip::kFifoData) {
        out[i] = fifoByte(fifo_bytes++);
      } else {
        int_status_read = int_status_read || (a == chip::kIntStatus);
        out[i] = readReg(a);
        a = static_cast<uint8_t>((a + 1U) & 0x7FU);
      }
    }
    if (int_status_read) {
      flags_ = 0; // clear-on-read; the FIFO flags follow their condition
    }
    popFifo(fifo_bytes);
    Elapse(kByteUs * (n + 1U));
  }

  /// A write burst from `start` (I2C auto-increment). Bytes from `limit` on are NACKed and not
  /// stored. Returns the bytes acknowledged; CMD = 0xB6 over I2C is applied and NACKed
  /// (section 7.36).
  std::size_t WriteBurst(uint8_t start, const uint8_t* data, std::size_t n, std::size_t limit,
                         bool i2c) noexcept {
    auto a = static_cast<uint8_t>(start & 0x7FU);
    std::size_t acked = 0;
    for (std::size_t i = 0; (i < n) && (i < limit); ++i) {
      const bool reset_cmd = (a == chip::kCmd) && (data[i] == chip::kSoftResetCommand);
      writeReg(a, data[i]);
      if (reset_cmd && i2c) {
        Elapse(kByteUs * (n + 1U));
        return acked;
      }
      ++acked;
      a = static_cast<uint8_t>((a + 1U) & 0x7FU);
    }
    Elapse(kByteUs * (n + 1U));
    return acked;
  }

  /// One SPI frame (section 5.5): the first frame after a reset with 16 or more SCK edges selects
  /// SPI and carries no valid data (section 5.1); then bit 7 of the first byte is the read flag, a
  /// read returns data from the second byte on, and a write is address and data pairs.
  void SpiFrame(const uint8_t* tx, uint8_t* rx, std::size_t len) noexcept {
    if (!spi_selected) {
      if (len >= 2U) {
        spi_selected = true;
      }
      FillOut(rx, len, select_fill);
      Elapse(kByteUs * (len + 1U));
      return;
    }
    if ((tx[0] & 0x80U) != 0U) {
      if (rx != nullptr) {
        rx[0] = 0xFFU;
        ReadBurst(static_cast<uint8_t>(tx[0] & 0x7FU), rx + 1, len - 1U);
      }
      return;
    }
    for (std::size_t i = 0; (i + 1U) < len; i += 2U) {
      (void)WriteBurst(static_cast<uint8_t>(tx[i] & 0x7FU), tx + i + 1U, 1U, 1U, false);
    }
    FillOut(rx, len, 0xFFU);
  }

  /// Fill a receive buffer (nothing when it is null).
  static void FillOut(uint8_t* rx, std::size_t len, uint8_t value) noexcept {
    if (rx != nullptr) {
      for (std::size_t i = 0; i < len; ++i) {
        rx[i] = value;
      }
    }
  }

private:
  /// A register byte widened for bit work.
  static uint32_t u(uint8_t v) noexcept {
    return v;
  }

  /// A new measuring mode, forced included, does not show yet (`mode_lag_us`).
  [[nodiscard]] bool lagging() const noexcept {
    return (mode_ != ModelMode::Standby) && (now_us < mode_shown_at_);
  }

  /// odr_is_valid does not show yet (`odr_valid_lag_us`).
  [[nodiscard]] bool validLagging() const noexcept {
    return (mode_ == ModelMode::Normal) && (now_us < valid_shown_at_);
  }

  /// ODR_CONFIG.odr.
  [[nodiscard]] uint32_t odrCode() const noexcept {
    return (u(regs_[chip::kOdrConfig]) >> 2U) & 0x1FU;
  }

  void loadResetValues() noexcept {
    regs_.fill(0);
    regs_[chip::kChipId] = chip_id;
    regs_[chip::kRevId] = chip::kRevIdValue;
    regs_[chip::kDriveConfig] = chip::kDriveConfigReset;
    regs_[chip::kIntConfig] = chip::kIntConfigReset;
    regs_[chip::kDspConfig] = chip::kDspConfigReset;
    regs_[chip::kOdrConfig] = chip::kOdrConfigReset;
    for (uint8_t a = chip::kTempXlsb; a <= 0x22U; ++a) {
      regs_[a] = chip::kDataResetByte;
    }
    mode_ = ModelMode::Standby;
    flags_ = chip::kPor;
    fifo_.clear();
    decimation_count_ = 0;
    oor_count_ = 0;
    standby_at_ = kNever;
    measure_at_ = kNever;
  }

  void reset() noexcept {
    loadResetValues();
    spi_selected = false;
    busy_until_ = now_us + reset_busy_us;
    nvm_ready_at_ = busy_until_ + nvm_boot_us;
    ++resets;
  }

  [[nodiscard]] bool deepStandby() const noexcept {
    const uint32_t odr = regs_[chip::kOdrConfig];
    return ((odr & chip::kDeepDis) == 0U) && (((odr >> 2U) & 0x1FU) >= 0x19U) &&
           ((u(regs_[chip::kFifoSel]) & 0x03U) == 0U) && ((u(regs_[chip::kDspIir]) & 0x3FU) == 0U);
  }

  [[nodiscard]] uint32_t conversionUs() const noexcept {
    const uint32_t osr = regs_[chip::kOsrConfig];
    return chip::kConvPUs[(osr >> 3U) & 7U] + chip::kConvTUs[osr & 7U];
  }

  [[nodiscard]] bool odrValid() const noexcept {
    const uint32_t osr = regs_[chip::kOsrConfig];
    const uint32_t max_mhz = chip::kMaxOdrHz[(osr >> 3U) & 7U][osr & 7U] * 1000U;
    return !reject_odr && (chip::kOdrMilliHz[odrCode()] <= max_mhz);
  }

  [[nodiscard]] uint8_t osrEff() const noexcept {
    const uint8_t osr = regs_[chip::kOsrConfig];
    if (odrValid()) {
      return static_cast<uint8_t>(0x80U | (osr & 0x3FU));
    }
    // Section 4.3.4: both 1x at 160 Hz and above, both 2x below.
    const bool fast = chip::kOdrMilliHz[odrCode()] >= 160000U;
    return fast ? uint8_t{0x00} : uint8_t{0x09};
  }

  [[nodiscard]] std::size_t frameBytes() const noexcept {
    const uint32_t sel = regs_[chip::kFifoSel] & 0x03U;
    if (sel == 0U) {
      return 0U;
    }
    return (sel == 3U) ? 6U : 3U;
  }

  [[nodiscard]] std::size_t capacity() const noexcept {
    return (frameBytes() == 6U) ? 16U : 32U;
  }

  [[nodiscard]] uint8_t fifoFlags() const noexcept {
    if (frameBytes() == 0U) {
      return 0;
    }
    uint8_t f = 0;
    if (fifo_.size() >= capacity()) {
      f = static_cast<uint8_t>(f | chip::kFifoFull);
    }
    const std::size_t ths = regs_[chip::kFifoConfig] & 0x1FU;
    if ((ths != 0U) && (fifo_.size() >= ths)) {
      f = static_cast<uint8_t>(f | chip::kFifoThs);
    }
    return f;
  }

  [[nodiscard]] uint8_t visible(uint8_t flags) const noexcept {
    const uint8_t enabled = flags_for_disabled_sources ? uint8_t{0x0F} : regs_[chip::kIntSource];
    return static_cast<uint8_t>(flags & (chip::kPor | (enabled & 0x0FU)));
  }

  uint8_t readReg(uint8_t a) noexcept {
    switch (a) {
    case chip::kIntStatus:
      return visible(static_cast<uint8_t>(flags_ | fifoFlags()));
    case chip::kStatus: {
      uint8_t s = (!nvm_never_ready && (now_us >= nvm_ready_at_)) ? chip::kNvmRdy : uint8_t{0};
      s = static_cast<uint8_t>(s | (nvm_error ? chip::kNvmErr : 0U) |
                               (nvm_cmd_error ? chip::kNvmCmdErr : 0U));
      return s;
    }
    case chip::kFifoCount:
      return static_cast<uint8_t>(fifo_.size());
    case chip::kOdrConfig:
      return static_cast<uint8_t>((u(regs_[a]) & 0xFCU) |
                                  (lagging() ? 0U : u(static_cast<uint8_t>(mode_))));
    case chip::kOsrEff:
      return (lagging() || validLagging()) ? static_cast<uint8_t>(u(osrEff()) & 0x7FU) : osrEff();
    case chip::kCmd:
      return 0;
    default:
      break;
    }
    return regs_[a];
  }

  /// Registers and the bits the host writes (section 7); 0 for read-only.
  static uint8_t writableMask(uint8_t a) noexcept {
    switch (a) {
    case chip::kDriveConfig:
    case chip::kIntConfig:
    case chip::kNvmDataLsb:
    case chip::kNvmDataMsb:
    case chip::kDspConfig:
    case chip::kOorThrPLsb:
    case chip::kOorThrPMsb:
    case chip::kOorRange:
    case chip::kOdrConfig:
      return 0xFF;
    case chip::kIntSource:
      return 0x0F;
    case chip::kFifoConfig:
      return 0x3F;
    case chip::kFifoSel:
      return 0x1F;
    case chip::kNvmAddr:
    case chip::kOsrConfig:
      return 0x7F;
    case chip::kDspIir:
      return 0x3F;
    case chip::kOorConfig:
      return 0xC1;
    default:
      break;
    }
    return 0;
  }

  /// Registers whose writes are lost outside standby (sections 4.3.8, 4.6.4, 7.24 to 7.32).
  static bool standbyOnly(uint8_t a) noexcept {
    return (a == chip::kFifoSel) || (a == chip::kDspConfig) || (a == chip::kDspIir) ||
           (a == chip::kOorConfig) || (a == chip::kNvmAddr) || (a == chip::kNvmDataLsb) ||
           (a == chip::kNvmDataMsb);
  }

  void writeReg(uint8_t a, uint8_t v) noexcept {
    if (a == chip::kCmd) {
      if (v == chip::kSoftResetCommand) {
        ++soft_resets;
        por_since_soft = false;
        reset();
      }
      return;
    }
    const bool standby_odr = (a == chip::kOdrConfig) && ((v & 0x03U) == 0U);
    if (por_since_soft && !standby_odr) {
      ++writes_over_reset;
    }
    const uint8_t mask = writableMask(a);
    if (deep_standby_drops_writes && (a != chip::kOdrConfig) && (mode_ == ModelMode::Standby) &&
        deepStandby()) {
      ++writes_lost_deep;
      return;
    }
    if (ignore_writes || (mask == 0U) || (standbyOnly(a) && (mode_ != ModelMode::Standby))) {
      return;
    }
    if (a == chip::kOdrConfig) {
      writeOdr(v);
      return;
    }
    const uint8_t old = regs_[a];
    regs_[a] = static_cast<uint8_t>(v & mask);
    if ((a == chip::kOsrConfig) && (old != regs_[a])) {
      reconfigure();
    }
    if ((a == chip::kFifoSel) && (old != regs_[a])) {
      fifo_.clear();
    }
  }

  /// A change of OSR or ODR in normal or continuous mode restarts the measurements after t_reconf
  /// and flushes the FIFO (sections 4.4.3, 4.6.4).
  void reconfigure() noexcept {
    fifo_.clear();
    if ((mode_ == ModelMode::Normal) || (mode_ == ModelMode::Continuous)) {
      measure_at_ = now_us + startup_us + conversionUs();
    }
  }

  void writeOdr(uint8_t v) noexcept {
    const uint8_t old = regs_[chip::kOdrConfig];
    regs_[chip::kOdrConfig] = static_cast<uint8_t>(v & 0xFCU);
    const auto req = static_cast<ModelMode>(v & 0x03U);
    if (((u(old) ^ u(v)) & 0x7CU) != 0U) {
      reconfigure();
    }
    if (req == ModelMode::Standby) {
      if ((mode_ != ModelMode::Standby) && (standby_at_ == kNever) && !standby_never) {
        standby_at_ = now_us + standby_delay_us;
        if (standby_delay_us == 0U) {
          mode_ = ModelMode::Standby;
          measure_at_ = kNever;
          standby_at_ = kNever;
        }
      }
      return;
    }
    if ((mode_ != ModelMode::Standby) || (standby_at_ != kNever)) {
      return; // a mode change goes through standby (section 4.3)
    }
    const uint32_t startup = deepStandby() ? startup_deep_us : startup_us;
    mode_ = req;
    mode_shown_at_ = now_us + mode_lag_us;
    valid_shown_at_ = now_us + odr_valid_lag_us;
    fifo_.clear(); // a pwr_mode change flushes the FIFO (section 4.6.4)
    measure_at_ = forced_never_ends && (req == ModelMode::Forced)
                      ? kNever
                      : now_us + startup + conversionUs();
  }

  void fire() noexcept {
    if (standby_at_ <= now_us) {
      mode_ = ModelMode::Standby;
      standby_at_ = kNever;
      measure_at_ = kNever;
      return;
    }
    produce();
    if (mode_ == ModelMode::Forced) {
      mode_ = ModelMode::Standby;
      measure_at_ = kNever;
    } else if (mode_ == ModelMode::Normal) {
      measure_at_ += chip::kOdrPeriodUs[odrCode()];
    } else {
      measure_at_ += conversionUs();
    }
  }

  static void put24(uint8_t* p, int32_t v) noexcept {
    const auto u = static_cast<uint32_t>(v);
    p[0] = static_cast<uint8_t>(u & 0xFFU);
    p[1] = static_cast<uint8_t>((u >> 8U) & 0xFFU);
    p[2] = static_cast<uint8_t>((u >> 16U) & 0xFFU);
  }

  /// A measurement completes (sections 4.5, 4.6, 4.7.2).
  void produce() noexcept {
    ++measurements;
    if (nvm_download_rewrites_data && deepStandby()) {
      regs_[chip::kNvmDataLsb] = nvm_download_data[0];
      regs_[chip::kNvmDataMsb] = nvm_download_data[1];
    }
    put24(&regs_[chip::kTempXlsb], temperature_raw);
    put24(&regs_[chip::kPressXlsb], pressure_raw);
    flags_ = static_cast<uint8_t>(flags_ | chip::kDrdy);
    pushFifo();
    checkOor();
  }

  void pushFifo() noexcept {
    const uint32_t sel = regs_[chip::kFifoSel] & 0x03U;
    if (sel == 0U) {
      return;
    }
    const uint32_t every = 1U << ((u(regs_[chip::kFifoSel]) >> 2U) & 0x07U);
    const bool keep = (decimation_count_ % every) == 0U;
    ++decimation_count_;
    if (!keep) {
      return;
    }
    std::array<uint8_t, 6> frame{};
    if (sel == 2U) {
      put24(frame.data(), pressure_raw);
    } else {
      put24(frame.data(), temperature_raw);
      put24(frame.data() + 3, pressure_raw);
    }
    if (fifo_.size() >= capacity()) {
      if ((regs_[chip::kFifoConfig] & 0x20U) != 0U) {
        return; // stop on full
      }
      fifo_.erase(fifo_.begin()); // streaming: the oldest frame goes
    }
    fifo_.push_back(frame);
  }

  void checkOor() noexcept {
    const uint32_t ref = static_cast<uint32_t>(regs_[chip::kOorThrPLsb]) |
                         (static_cast<uint32_t>(regs_[chip::kOorThrPMsb]) << 8U) |
                         ((static_cast<uint32_t>(regs_[chip::kOorConfig]) & 1U) << 16U);
    const int32_t pa = pressure_raw / 64;
    const int32_t range = regs_[chip::kOorRange];
    const bool out =
        (pa > (static_cast<int32_t>(ref) + range)) || (pa < (static_cast<int32_t>(ref) - range));
    oor_count_ = out ? (oor_count_ + 1U) : 0U;
    const uint32_t limit = (2U << ((u(regs_[chip::kOorConfig]) >> 6U) & 3U)) - 1U; // 1, 3, 7, 15
    if (oor_count_ >= limit) {
      flags_ = static_cast<uint8_t>(flags_ | chip::kOorP);
    }
  }

  [[nodiscard]] uint8_t fifoByte(std::size_t k) const noexcept {
    const std::size_t fb = frameBytes();
    if ((fb == 0U) || ((k / fb) >= fifo_.size())) {
      return chip::kFifoEmptyByte;
    }
    return fifo_[k / fb][k % fb];
  }

  /// Whole frames read leave the FIFO; a partly read frame stays (section 4.6.3).
  void popFifo(std::size_t bytes) noexcept {
    const std::size_t fb = frameBytes();
    if (fb == 0U) {
      return;
    }
    std::size_t frames = bytes / fb;
    while ((frames > 0U) && !fifo_.empty()) {
      fifo_.erase(fifo_.begin());
      --frames;
    }
  }

  std::array<uint8_t, 128> regs_{};
  ModelMode mode_ = ModelMode::Standby;
  uint8_t flags_ = 0;
  uint64_t busy_until_ = 0;
  uint64_t nvm_ready_at_ = 0;
  uint64_t standby_at_ = kNever;
  uint64_t measure_at_ = kNever;
  uint64_t mode_shown_at_ = 0;
  uint64_t valid_shown_at_ = 0;
  uint32_t decimation_count_ = 0;
  uint32_t oor_count_ = 0;
  std::vector<std::array<uint8_t, 6>> fifo_;
};

} // namespace bmp581_test

#include "mock_bmp581_bus.hpp" // IWYU pragma: export

#endif // BMP581_MOCK_BMP581_HPP
