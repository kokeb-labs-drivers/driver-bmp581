/**
 * @file bmp581_types.hpp
 * @brief Public types of the BMP581 driver: errors, `Result<T>`, configuration, measurements and
 * the datasheet's timing and range constants.
 * @copyright Copyright (c) 2026 The driver-bmp581 authors. SPDX-License-Identifier: MIT
 *
 * @details
 * Everything here is plain data with no platform dependency, so handlers, tests and tools can
 * include it without the driver template. The configuration defaults are the chip's reset values
 * (datasheet section 7), so `Config{}` describes a chip that was just reset, with one exception the
 * driver always applies: pressure is measured (`OSR_CONFIG.press_en = 1`, see `bmp581.hpp`).
 *
 * Section, table and figure numbers refer to the Bosch BMP581 datasheet, document
 * BST-BMP581-DS004-13, revision 1.13.
 */
#ifndef BMP581_TYPES_HPP
#define BMP581_TYPES_HPP

#include <array>
#include <cstdint>

#include "bmp581_detail.hpp"
#include "bmp581_version.h"

/** @brief The BMP581 driver: everything it declares lives here; internals in `detail`. */
namespace bmp581 {

/**
 * @defgroup bmp581_types Driver types
 * @brief Errors, results, configuration and measurements.
 * @{
 */

/**
 * @brief Why a call failed. Every call of the driver returns one of these in its `Result`.
 *
 * Values 0 to 9 are the shared base set; chip-specific values start at 16. `[[nodiscard]]` on
 * the type: a function that returns a bare `Error` cannot have it ignored either (KB-CPP20-037).
 */
enum class [[nodiscard]] Error : uint8_t {
  // Base set: the same names and values in every driver of the org (standards/driver.md).
  Ok = 0,               ///< The call succeeded.
  NotInitialized = 1,   ///< `EnsureInitialized()` has not succeeded, or `Deinitialize()` ran or
                        ///< a reset was found since.
  InvalidArgument = 2,  ///< An argument, the address or a `Config` field is out of range; nothing
                        ///< was sent.
  Bus = 3,              ///< A port hook returned `false`: a bus error or a NACK the port cannot
                        ///< place.
  Timeout = 4,          ///< The chip did not become ready (NVM), finish a forced measurement, reach
                        ///< standby or report data ready within the allowed time.
  DeviceIdMismatch = 5, ///< At start-up, CHIP_ID did not read 0x50, or INT_STATUS did not show the
                        ///< soft reset (0x10, then 0x00): no BMP581 answers.
  Crc = 6,              ///< Not returned by this driver (the bus carries no CRC).
  Busy = 7,             ///< A forced measurement needs standby, and the chip measures (normal or
                        ///< continuous mode, or a forced measurement still runs); nothing was sent.
  DeviceFault = 8,      ///< At start-up, STATUS reports an NVM error (status_nvm_err or
                        ///< status_nvm_cmd_err): the trimming data did not load.
  Unsupported = 9,      ///< `WaitDataReady()` without the data-ready source in `Config`: the
                        ///< chip reports data ready only for an enabled source (section 4.7.1).
  // Chip-specific values start at 16; each one is returned by some call (tests/test_errors.cpp).
  DeviceReset = 16,    ///< The chip went through a power-on or soft reset since the start,
                       ///< confirmed by a second read: OSR_CONFIG.press_en reads 0 (the driver
                       ///< always writes 1), seen directly or after INT_STATUS.por. The driver is
                       ///< uninitialised and wrote nothing over the reset chip: call
                       ///< `EnsureInitialized()`.
  InvalidReply = 17,   ///< A read returned what the chip cannot send: a reserved bit set, a
                       ///< reserved register not 0x00, a frame of all 0x00 or all 0xFF, a FIFO
                       ///< count above its capacity, or flags that contradict each other. A
                       ///< floating, stuck or disturbed line; never data.
  NoDevice = 18,       ///< Nothing acknowledged the I2C address byte: no chip at the address, or
                       ///< it is unpowered. Needs a port with the optional `GetLastFailure()`
                       ///< hook; without it this is `Bus`.
  NoData = 19,         ///< The data registers or a FIFO frame hold their reset value 0x7F7F7F: no
                       ///< measurement since the reset (sections 7.11 to 7.16, 4.6.3).
  OutOfRange = 20,     ///< A decoded value is outside the measurement range, 30 to 125 kPa or
                       ///< -40 to 85 °C (tables 1 and 2): returned as an error, never clamped.
  WriteMismatch = 21,  ///< A register read back differently from what was just written: a
                       ///< write the chip acknowledged but did not keep. The configuration is
                       ///< then in doubt (`IsConfigKnown()`).
  StateUnknown = 22,   ///< The call depends on the chip's configuration, which a failed write
                       ///< left in doubt; nothing was sent. `ApplyConfig()` or
                       ///< `EnsureInitialized()` makes it known again.
  ConfigRejected = 23, ///< In normal mode the chip found the data rate too fast for the
                       ///< oversampling (OSR_EFF.odr_is_valid = 0) and measures with its own
                       ///< defaults (section 4.3.4). The chip was put back in standby.
};

/**
 * @brief The name of an error, for logs.
 * @param error The error.
 * @return A static string such as `"Timeout"`; `"Unknown"` for a value that is not an enumerator.
 */
constexpr const char* ToString(Error error) noexcept {
  switch (error) {
  case Error::Ok:
    return "Ok";
  case Error::NotInitialized:
    return "NotInitialized";
  case Error::InvalidArgument:
    return "InvalidArgument";
  case Error::Bus:
    return "Bus";
  case Error::Timeout:
    return "Timeout";
  case Error::DeviceIdMismatch:
    return "DeviceIdMismatch";
  case Error::Crc:
    return "Crc";
  case Error::Busy:
    return "Busy";
  case Error::DeviceFault:
    return "DeviceFault";
  case Error::Unsupported:
    return "Unsupported";
  case Error::DeviceReset:
    return "DeviceReset";
  case Error::InvalidReply:
    return "InvalidReply";
  case Error::NoDevice:
    return "NoDevice";
  case Error::NoData:
    return "NoData";
  case Error::OutOfRange:
    return "OutOfRange";
  case Error::WriteMismatch:
    return "WriteMismatch";
  case Error::StateUnknown:
    return "StateUnknown";
  case Error::ConfigRejected:
    return "ConfigRejected";
  default:
    break;
  }
  return "Unknown";
}

/**
 * @brief The value of a call, or why there is none.
 *
 * Check `ok()` (or the `bool` conversion) before reading `value`; on failure `value` is `T{}`.
 *
 * @tparam T The value type. `Result<void>` carries only the error.
 */
template <typename T>
struct [[nodiscard]] Result {
  T value{};              ///< The value; `T{}` when the call failed.
  Error error{Error::Ok}; ///< `Error::Ok` on success.

  /** @return true when the call succeeded. */
  [[nodiscard]] constexpr bool ok() const noexcept {
    return error == Error::Ok;
  }
  /** @return true when the call succeeded. */
  constexpr explicit operator bool() const noexcept {
    return ok();
  }

  /**
   * @brief A successful result.
   * @param v The value.
   * @return `{v, Error::Ok}`.
   */
  static constexpr Result Success(T v) noexcept {
    return {v, Error::Ok};
  }
  /**
   * @brief A failed result.
   * @param e Why it failed; never `Error::Ok`.
   * @return `{T{}, e}`.
   */
  static constexpr Result Failure(Error e) noexcept {
    return {T{}, e};
  }
};

/** @brief The result of a call that returns no value. */
template <>
struct [[nodiscard]] Result<void> {
  Error error{Error::Ok}; ///< `Error::Ok` on success.

  /** @return true when the call succeeded. */
  [[nodiscard]] constexpr bool ok() const noexcept {
    return error == Error::Ok;
  }
  /** @return true when the call succeeded. */
  constexpr explicit operator bool() const noexcept {
    return ok();
  }

  /** @return A successful result. */
  static constexpr Result Success() noexcept {
    return {Error::Ok};
  }
  /**
   * @brief A failed result.
   * @param e Why it failed; never `Error::Ok`.
   * @return `{e}`.
   */
  static constexpr Result Failure(Error e) noexcept {
    return {e};
  }
};

/** @brief Severity passed to the optional `Log()` hook. */
enum class LogLevel : uint8_t {
  Error = 0, ///< A call failed.
  Warning,   ///< Something unexpected that the driver recovered from.
  Info,      ///< Lifecycle events: initialised, reset.
  Debug,     ///< Detail for bring-up.
};

/** @brief Extra traffic a call may spend to check what it did (handbook KB-ARCH-041). */
enum class Verify : uint8_t {
  None = 0,     ///< Only the traffic the datasheet needs.
  ReadBack = 1, ///< Also the reset check (one 3-byte read of OSR_CONFIG to OSR_EFF) and, for a
                ///< measurement, a second read of the data registers.
};

/** @brief The power mode, ODR_CONFIG.pwr_mode (sections 4.3, 7.34). */
enum class PowerMode : uint8_t {
  Standby = 0,    ///< No measurement; deep standby when its conditions hold (section 4.3.2).
                  ///< Reset value.
  Normal = 1,     ///< One measurement per output data rate period.
  Forced = 2,     ///< One measurement, then standby: `MeasureOnce()`, never a `Config` mode.
  Continuous = 3, ///< Back-to-back measurements as fast as the oversampling allows.
};

/** @brief Oversampling ratio, OSR_CONFIG.osr_p and osr_t (section 7.33). */
enum class Oversampling : uint8_t {
  X1 = 0, ///< 1 sample. Reset value.
  X2,     ///< 2 samples.
  X4,     ///< 4 samples.
  X8,     ///< 8 samples.
  X16,    ///< 16 samples.
  X32,    ///< 32 samples.
  X64,    ///< 64 samples.
  X128,   ///< 128 samples.
};

/** @brief Output data rate in normal mode, ODR_CONFIG.odr (section 7.34). */
enum class OutputDataRate : uint8_t {
  Hz240 = 0x00,   ///< 240.000 Hz.
  Hz218p5 = 0x01, ///< 218.537 Hz.
  Hz199p1 = 0x02, ///< 199.111 Hz.
  Hz179p2 = 0x03, ///< 179.200 Hz.
  Hz160 = 0x04,   ///< 160.000 Hz.
  Hz149p3 = 0x05, ///< 149.333 Hz.
  Hz140 = 0x06,   ///< 140.000 Hz.
  Hz129p9 = 0x07, ///< 129.855 Hz.
  Hz120 = 0x08,   ///< 120.000 Hz.
  Hz110p2 = 0x09, ///< 110.164 Hz.
  Hz100p3 = 0x0A, ///< 100.299 Hz.
  Hz89p6 = 0x0B,  ///< 89.600 Hz.
  Hz80 = 0x0C,    ///< 80.000 Hz.
  Hz70 = 0x0D,    ///< 70.000 Hz.
  Hz60 = 0x0E,    ///< 60.000 Hz.
  Hz50p1 = 0x0F,  ///< 50.056 Hz.
  Hz45 = 0x10,    ///< 45.025 Hz.
  Hz40 = 0x11,    ///< 40.000 Hz.
  Hz35 = 0x12,    ///< 35.000 Hz.
  Hz30 = 0x13,    ///< 30.000 Hz.
  Hz25 = 0x14,    ///< 25.005 Hz.
  Hz20 = 0x15,    ///< 20.000 Hz.
  Hz15 = 0x16,    ///< 15.000 Hz.
  Hz10 = 0x17,    ///< 10.000 Hz.
  Hz5 = 0x18,     ///< 5.000 Hz.
  Hz4 = 0x19,     ///< 4.000 Hz: the fastest rate with deep standby between measurements.
  Hz3 = 0x1A,     ///< 3.000 Hz.
  Hz2 = 0x1B,     ///< 2.000 Hz.
  Hz1 = 0x1C,     ///< 1.000 Hz. Reset value.
  Hz0p5 = 0x1D,   ///< 0.500 Hz.
  Hz0p25 = 0x1E,  ///< 0.250 Hz.
  Hz0p125 = 0x1F, ///< 0.125 Hz.
};

/** @brief IIR low-pass filter coefficient, DSP_IIR.set_iir_p and set_iir_t (table 10). */
enum class IirFilter : uint8_t {
  Bypass = 0, ///< No filter. Reset value.
  Coef1,      ///< Coefficient 1.
  Coef3,      ///< Coefficient 3.
  Coef7,      ///< Coefficient 7.
  Coef15,     ///< Coefficient 15.
  Coef31,     ///< Coefficient 31.
  Coef63,     ///< Coefficient 63.
  Coef127,    ///< Coefficient 127.
};

/** @brief What the FIFO stores, FIFO_SEL.fifo_frame_sel (section 4.6.1). */
enum class FifoFrame : uint8_t {
  Disabled = 0,               ///< FIFO off. Reset value.
  Temperature = 1,            ///< 3-byte temperature frames, up to 32.
  Pressure = 2,               ///< 3-byte pressure frames, up to 32.
  PressureAndTemperature = 3, ///< 6-byte frames, temperature first, up to 16.
};

/** @brief What a full FIFO does with a new frame, FIFO_CONFIG.fifo_mode (section 4.6.1). */
enum class FifoMode : uint8_t {
  Streaming = 0,  ///< The oldest frame is dropped. Reset value.
  StopOnFull = 1, ///< The new frame is dropped.
};

/** @brief INT pin timing, INT_CONFIG.int_mode (section 4.7.3.2). */
enum class IntMode : uint8_t {
  Pulsed = 0,  ///< A pulse of t_int_pulse per event.
  Latched = 1, ///< Asserted while a condition holds. Reset value.
};

/** @brief INT pin polarity, INT_CONFIG.int_pol (section 7.5). */
enum class IntPolarity : uint8_t {
  ActiveLow = 0,  ///< Reset value.
  ActiveHigh = 1, ///< Asserted high.
};

/** @brief INT pin output stage, INT_CONFIG.int_od (section 7.5). */
enum class IntDrive : uint8_t {
  PushPull = 0,  ///< Driven both ways.
  OpenDrain = 1, ///< Needs a pull-up. Reset value.
};

/** @brief Consecutive out-of-range samples before the interrupt, OOR_CONFIG.cnt_lim (7.32). */
enum class OorCount : uint8_t {
  Samples1 = 0, ///< 1 sample. Reset value.
  Samples3,     ///< 3 samples.
  Samples7,     ///< 7 samples.
  Samples15,    ///< 15 samples.
};

/**
 * @brief The interrupt status flags, INT_STATUS (section 7.21), decoded. Reading INT_STATUS clears
 * them on the chip (the FIFO flags stay while their condition holds, section 4.7.3.2).
 */
struct InterruptStatus {
  bool data_ready{false};     ///< drdy_data_reg: new data in the data registers and the FIFO.
  bool fifo_full{false};      ///< fifo_full.
  bool fifo_threshold{false}; ///< fifo_ths: the FIFO holds at least the threshold.
  bool out_of_range{false};   ///< oor_p: pressure outside the window.
  bool power_on_reset{false}; ///< por: a power-on or soft reset completed.
};

/**
 * @brief One measurement in the chip's own units, as the data registers hold it (section 4.5).
 *
 * The chip compensates on chip; the driver only sign-extends the 24-bit values. `PressurePa()` and
 * `TemperatureC()` divide exactly (a float holds every 24-bit value).
 */
struct Measurement {
  int32_t pressure_raw{0};    ///< Pressure in 1/64 Pa.
  int32_t temperature_raw{0}; ///< Temperature in 1/65536 °C.
};

/** @brief One FIFO frame, with its own validity: a multi-item read reports each item. */
struct FifoSample {
  Error error{Error::Ok};   ///< `Ok`, `NoData` (0x7F7F7F) or `OutOfRange`; the values are 0 then.
  bool has_pressure{false}; ///< The frame carries a pressure (P and PT frames).
  bool has_temperature{false}; ///< The frame carries a temperature (T and PT frames).
  int32_t pressure_raw{0};     ///< Pressure in 1/64 Pa.
  int32_t temperature_raw{0};  ///< Temperature in 1/65536 °C.
};

/// CHIP_ID of every BMP581 (section 7.1).
inline constexpr uint8_t kChipId = 0x50;
/// Pressure LSBs per pascal: the registers hold Pa x 2^6 (section 4.5).
inline constexpr int32_t kPressureCountsPerPa = 64;
/// Temperature LSBs per degree Celsius: the registers hold °C x 2^16 (section 4.5).
inline constexpr int32_t kTemperatureCountsPerDegreeC = 65536;
/// Lowest pressure the chip measures, in pascals (table 1).
inline constexpr int32_t kPressureMinPa = 30'000;
/// Highest pressure the chip measures, in pascals (table 1).
inline constexpr int32_t kPressureMaxPa = 125'000;
/// Lowest temperature the chip measures, in °C (table 2).
inline constexpr int32_t kTemperatureMinC = -40;
/// Highest temperature the chip measures, in °C (table 2).
inline constexpr int32_t kTemperatureMaxC = 85;
/// Highest out-of-range reference: 17 bits of pascals (section 4.7.2.3).
inline constexpr uint32_t kOorReferenceMaxPa = 0x1'FFFF;
/// Frames the FIFO holds with 3-byte frames (section 4.6).
inline constexpr uint32_t kFifoFramesSingle = 32;
/// Frames the FIFO holds with 6-byte frames (section 4.6).
inline constexpr uint32_t kFifoFramesBoth = 16;
/// Bytes of a full FIFO, either frame size.
inline constexpr uint32_t kFifoMaxBytes = 96;

/// t_powup: power-up to the first communication, maximum (table 4).
inline constexpr uint32_t kPowerUpUs = 2000;
/// t_soft_res: soft reset to ready, maximum (table 4).
inline constexpr uint32_t kSoftResetUs = 2000;
/// t_standby: any mode to standby, maximum (table 4).
inline constexpr uint32_t kStandbyUs = 2500;
/// t_startup: standby to the start of a measurement, maximum (table 4).
inline constexpr uint32_t kStartupUs = 3000;
/// t_startup_deep: deep standby to the start of a measurement, maximum (table 4).
inline constexpr uint32_t kStartupDeepUs = 4000;
/// t_reconf: a configuration change to the first measurement, maximum (table 4); the FIFO flush
/// completes within it (section 4.6.4).
inline constexpr uint32_t kReconfUs = 3000;
/// Idle time before INT_CONFIG may be read back after a write (sections 5.5.4, 7.5).
inline constexpr uint32_t kReadBackIdleUs = 1;
/// Time between two polls of the chip.
inline constexpr uint32_t kPollUs = 500;
/// Longest `Config::startup_timeout_ms` and `Config::data_ready_timeout_ms` the driver accepts: the
/// bound of every poll loop (KB-CPP20-058; the Kconfig ranges are the same).
inline constexpr uint32_t kTimeoutMaxMs = 10'000;

/**
 * @brief Everything `EnsureInitialized()` and `ApplyConfig()` write to the chip.
 *
 * The defaults are the reset values of the datasheet (section 7), so `Config{}` changes nothing on
 * a freshly reset chip except that pressure is measured. Fields with a unit carry it as a suffix.
 * Integral and enumeration members only: two configurations compare with `==`.
 */
struct Config {
  PowerMode mode{PowerMode::Standby};                      ///< Standby, Normal or Continuous.
  Oversampling pressure_oversampling{Oversampling::X1};    ///< OSR_CONFIG.osr_p.
  Oversampling temperature_oversampling{Oversampling::X1}; ///< OSR_CONFIG.osr_t.
  OutputDataRate odr{OutputDataRate::Hz1};                 ///< ODR_CONFIG.odr (normal mode).
  bool deep_standby{true}; ///< ODR_CONFIG.deep_dis = 0: deep standby where its conditions hold.
  IirFilter pressure_iir{IirFilter::Bypass};    ///< DSP_IIR.set_iir_p.
  IirFilter temperature_iir{IirFilter::Bypass}; ///< DSP_IIR.set_iir_t.
  bool iir_to_data_registers{false}; ///< DSP_CONFIG.shdw_sel_iir_t and _p: filtered data.
  bool iir_to_fifo{false};           ///< DSP_CONFIG.fifo_sel_iir_t and _p: filtered FIFO frames.
  bool iir_to_out_of_range{false};   ///< DSP_CONFIG.oor_sel_iir_p: filtered OOR check.
  bool iir_flush_in_forced{false};   ///< DSP_CONFIG.iir_flush_forced_en.
  FifoFrame fifo_frame{FifoFrame::Disabled}; ///< FIFO_SEL.fifo_frame_sel.
  FifoMode fifo_mode{FifoMode::Streaming};   ///< FIFO_CONFIG.fifo_mode.
  uint8_t fifo_threshold{0};  ///< FIFO_CONFIG.fifo_threshold: 0 off, 1 to 31 (15 for PT frames).
  uint8_t fifo_decimation{0}; ///< FIFO_SEL.fifo_dec_sel: keep every 2^n-th frame, 0 to 7.
  bool int_pin_enable{false}; ///< INT_CONFIG.int_en.
  IntMode int_mode{IntMode::Latched};               ///< INT_CONFIG.int_mode.
  IntPolarity int_polarity{IntPolarity::ActiveLow}; ///< INT_CONFIG.int_pol.
  IntDrive int_drive{IntDrive::OpenDrain};          ///< INT_CONFIG.int_od.
  uint8_t int_pad_drive{3}; ///< INT_CONFIG.pad_int_drv, 0 to 15; set bit 3 only with open drain.
  bool source_data_ready{false};     ///< INT_SOURCE.drdy_data_reg_en; `WaitDataReady()` needs it.
  bool source_fifo_full{false};      ///< INT_SOURCE.fifo_full_en.
  bool source_fifo_threshold{false}; ///< INT_SOURCE.fifo_ths_en.
  bool source_out_of_range{false};   ///< INT_SOURCE.oor_p_en.
  uint32_t oor_reference_pa{0};      ///< OOR_THR_P and OOR_CONFIG bit 0: 0 to `kOorReferenceMaxPa`.
  uint8_t oor_window_pa{0};          ///< OOR_RANGE: +/- this many pascals around the reference.
  OorCount oor_count{OorCount::Samples1}; ///< OOR_CONFIG.cnt_lim.
  uint32_t startup_timeout_ms{10};        ///< Driver side: longest wait for STATUS.status_nvm_rdy
                                          ///< at start-up, 1 to `kTimeoutMaxMs`.
  uint32_t data_ready_timeout_ms{2000};   ///< Driver side: longest `WaitDataReady()`, 1 to
                                          ///< `kTimeoutMaxMs`; cover one data rate period.

  /// Exact comparison: every member is integral or an enumeration (KB-CPP20-033).
  friend constexpr bool operator==(const Config& lhs, const Config& rhs) noexcept = default;
};

namespace detail {
/// Last enumerator of `Verify` (detail::IsEnumerator).
template <>
struct EnumLast<Verify> {
  static constexpr Verify kValue = Verify::ReadBack; ///< The last enumerator.
};
/// Last enumerator of `PowerMode`.
template <>
struct EnumLast<PowerMode> {
  static constexpr PowerMode kValue = PowerMode::Continuous; ///< The last enumerator.
};
/// Last enumerator of `Oversampling`.
template <>
struct EnumLast<Oversampling> {
  static constexpr Oversampling kValue = Oversampling::X128; ///< The last enumerator.
};
/// Last enumerator of `OutputDataRate`.
template <>
struct EnumLast<OutputDataRate> {
  static constexpr OutputDataRate kValue = OutputDataRate::Hz0p125; ///< The last enumerator.
};
/// Last enumerator of `IirFilter`.
template <>
struct EnumLast<IirFilter> {
  static constexpr IirFilter kValue = IirFilter::Coef127; ///< The last enumerator.
};
/// Last enumerator of `FifoFrame`.
template <>
struct EnumLast<FifoFrame> {
  static constexpr FifoFrame kValue = FifoFrame::PressureAndTemperature; ///< The last enumerator.
};
/// Last enumerator of `FifoMode`.
template <>
struct EnumLast<FifoMode> {
  static constexpr FifoMode kValue = FifoMode::StopOnFull; ///< The last enumerator.
};
/// Last enumerator of `IntMode`.
template <>
struct EnumLast<IntMode> {
  static constexpr IntMode kValue = IntMode::Latched; ///< The last enumerator.
};
/// Last enumerator of `IntPolarity`.
template <>
struct EnumLast<IntPolarity> {
  static constexpr IntPolarity kValue = IntPolarity::ActiveHigh; ///< The last enumerator.
};
/// Last enumerator of `IntDrive`.
template <>
struct EnumLast<IntDrive> {
  static constexpr IntDrive kValue = IntDrive::OpenDrain; ///< The last enumerator.
};
/// Last enumerator of `OorCount`.
template <>
struct EnumLast<OorCount> {
  static constexpr OorCount kValue = OorCount::Samples15; ///< The last enumerator.
};

/// Output data rates in millihertz, by ODR code (section 7.34).
inline constexpr std::array<uint32_t, 32> kOdrMilliHz{
    240'000U, 218'537U, 199'111U, 179'200U, 160'000U, 149'333U, 140'000U, 129'855U,
    120'000U, 110'164U, 100'299U, 89'600U,  80'000U,  70'000U,  60'000U,  50'056U,
    45'025U,  40'000U,  35'000U,  30'000U,  25'005U,  20'000U,  15'000U,  10'000U,
    5'000U,   4'000U,   3'000U,   2'000U,   1'000U,   500U,     250U,     125U};

/// Table 7: the highest nominal data rate in normal mode, in hertz, by [osr_p][osr_t].
inline constexpr std::array<std::array<uint16_t, 8>, 8> kMaxOdrHz{{
    {240U, 240U, 240U, 240U, 200U, 130U, 80U, 40U},
    {240U, 240U, 240U, 220U, 180U, 120U, 70U, 40U},
    {220U, 220U, 200U, 180U, 140U, 100U, 70U, 40U},
    {140U, 140U, 130U, 120U, 100U, 80U, 50U, 35U},
    {80U, 80U, 80U, 70U, 70U, 50U, 45U, 30U},
    {45U, 45U, 40U, 40U, 40U, 35U, 30U, 20U},
    {20U, 20U, 20U, 20U, 20U, 20U, 15U, 15U},
    {10U, 10U, 10U, 10U, 10U, 10U, 10U, 5U},
}};

/// t_conv_p, typical, in microseconds by oversampling (table 4).
inline constexpr std::array<uint32_t, 8> kConvPressureUs{1000U,  1700U,  2900U,  5400U,
                                                         10400U, 20400U, 40400U, 80400U};
/// t_conv_t, typical, in microseconds by oversampling (table 4).
inline constexpr std::array<uint32_t, 8> kConvTemperatureUs{1000U, 1100U, 1500U,  2100U,
                                                            3300U, 5800U, 10800U, 20800U};
} // namespace detail

/**
 * @brief An output data rate in millihertz (section 7.34).
 * @param odr The rate; a value that is not an enumerator reads as the slowest rate.
 * @return The rate, for example 240000 for `Hz240`.
 */
constexpr uint32_t OdrMilliHz(OutputDataRate odr) noexcept {
  return detail::kOdrMilliHz[static_cast<uint32_t>(odr) & 0x1FU]; // masked: 32 codes
}

/**
 * @brief The fastest data rate normal mode accepts with this oversampling (table 7).
 * @param pressure OSR_CONFIG.osr_p.
 * @param temperature OSR_CONFIG.osr_t.
 * @return The rate in millihertz.
 */
constexpr uint32_t MaxOdrMilliHz(Oversampling pressure, Oversampling temperature) noexcept {
  const uint32_t p = static_cast<uint32_t>(pressure) & 0x07U; // masked: 8 settings
  const uint32_t t = static_cast<uint32_t>(temperature) & 0x07U;
  return static_cast<uint32_t>(detail::kMaxOdrHz[p][t]) * 1000U;
}

/**
 * @brief The longest pressure-and-temperature conversion, t_conv_p + t_conv_t, with the +5 % of
 * table 4, rounded up.
 * @param pressure OSR_CONFIG.osr_p.
 * @param temperature OSR_CONFIG.osr_t.
 * @return Microseconds.
 */
constexpr uint32_t ConversionTimeMaxUs(Oversampling pressure, Oversampling temperature) noexcept {
  const uint32_t typical = detail::kConvPressureUs[static_cast<uint32_t>(pressure) & 0x07U] +
                           detail::kConvTemperatureUs[static_cast<uint32_t>(temperature) & 0x07U];
  return ((typical * 105U) + 99U) / 100U;
}

/**
 * @brief Whether the chip enters deep standby with this configuration (section 4.3.2): deep standby
 * allowed, a data rate below 5 Hz, the FIFO off and both IIR filters bypassed.
 * @param config The configuration.
 * @return true when standby phases are deep standby.
 */
constexpr bool EntersDeepStandby(const Config& config) noexcept {
  return config.deep_standby &&
         (static_cast<uint32_t>(config.odr) > static_cast<uint32_t>(OutputDataRate::Hz5)) &&
         (config.fifo_frame == FifoFrame::Disabled) && (config.pressure_iir == IirFilter::Bypass) &&
         (config.temperature_iir == IirFilter::Bypass);
}

/**
 * @brief The longest forced measurement: the start-up from (deep) standby, then the conversion
 * (table 4).
 * @param config The configuration.
 * @return Microseconds.
 */
constexpr uint32_t ForcedMeasurementMaxUs(const Config& config) noexcept {
  const uint32_t startup = EntersDeepStandby(config) ? kStartupDeepUs : kStartupUs;
  return startup +
         ConversionTimeMaxUs(config.pressure_oversampling, config.temperature_oversampling);
}

/**
 * @brief A 24-bit two's complement register value as a signed integer, the one place the driver
 * reinterprets wire bits as signed (KB-CPP20-024).
 * @param raw The 24 bits; bits 31..24 are ignored.
 * @return -8388608 to 8388607.
 */
constexpr int32_t SignExtend24(uint32_t raw) noexcept {
  const uint32_t bits = raw & 0x00FF'FFFFU;
  return static_cast<int32_t>(bits ^ 0x0080'0000U) - 0x0080'0000;
}

/**
 * @brief A pressure in pascals: `raw / 2^6`, exact (section 4.5).
 * @param raw Pressure in 1/64 Pa.
 * @return Pascals.
 */
constexpr float PressurePa(int32_t raw) noexcept {
  return static_cast<float>(raw) / static_cast<float>(kPressureCountsPerPa);
}

/**
 * @brief A temperature in degrees Celsius: `raw / 2^16`, exact (section 4.5).
 * @param raw Temperature in 1/65536 °C.
 * @return Degrees Celsius.
 */
constexpr float TemperatureC(int32_t raw) noexcept {
  return static_cast<float>(raw) / static_cast<float>(kTemperatureCountsPerDegreeC);
}

/**
 * @brief Whether a raw pressure lies in the measurement range, 30 to 125 kPa (table 1).
 * @param raw Pressure in 1/64 Pa.
 * @return true inside the range, both ends included.
 */
constexpr bool PressureInRange(int32_t raw) noexcept {
  return (raw >= (kPressureMinPa * kPressureCountsPerPa)) &&
         (raw <= (kPressureMaxPa * kPressureCountsPerPa));
}

/**
 * @brief Whether a raw temperature lies in the measurement range, -40 to 85 °C (table 2).
 * @param raw Temperature in 1/65536 °C.
 * @return true inside the range, both ends included.
 */
constexpr bool TemperatureInRange(int32_t raw) noexcept {
  return (raw >= (kTemperatureMinC * kTemperatureCountsPerDegreeC)) &&
         (raw <= (kTemperatureMaxC * kTemperatureCountsPerDegreeC));
}

namespace detail {
/// Whether every enumeration of a configuration holds an enumerator (KB-CPP20-025).
constexpr bool EnumeratorsValid(const Config& c) noexcept {
  return IsEnumerator(c.mode) && IsEnumerator(c.pressure_oversampling) &&
         IsEnumerator(c.temperature_oversampling) && IsEnumerator(c.odr) &&
         IsEnumerator(c.pressure_iir) && IsEnumerator(c.temperature_iir) &&
         IsEnumerator(c.fifo_frame) && IsEnumerator(c.fifo_mode) && IsEnumerator(c.int_mode) &&
         IsEnumerator(c.int_polarity) && IsEnumerator(c.int_drive) && IsEnumerator(c.oor_count);
}

/// Whether the integral fields of a configuration are in their documented ranges: the FIFO
/// threshold (15 with pressure-and-temperature frames, section 4.6.1), the decimation, the INT pad
/// drive (bit 3 with open drain only, section 7.5), the 17-bit reference, and both timeouts, 1 to
/// `kTimeoutMaxMs` (KB-CPP20-058).
constexpr bool RangesValid(const Config& c) noexcept {
  const uint32_t threshold_max = (c.fifo_frame == FifoFrame::PressureAndTemperature) ? 15U : 31U;
  const bool fields =
      (static_cast<uint32_t>(c.fifo_threshold) <= threshold_max) &&
      (static_cast<uint32_t>(c.fifo_decimation) <= 7U) &&
      (static_cast<uint32_t>(c.int_pad_drive) <= 15U) &&
      ((c.int_drive == IntDrive::OpenDrain) || (static_cast<uint32_t>(c.int_pad_drive) <= 7U)) &&
      (c.oor_reference_pa <= kOorReferenceMaxPa);
  const bool timeouts = (c.startup_timeout_ms >= 1U) && (c.startup_timeout_ms <= kTimeoutMaxMs) &&
                        (c.data_ready_timeout_ms >= 1U) &&
                        (c.data_ready_timeout_ms <= kTimeoutMaxMs);
  return fields && timeouts;
}
} // namespace detail

/**
 * @brief Check a configuration without touching the chip.
 *
 * Fails when an enumeration holds a value that is not an enumerator, the mode is `Forced` (use
 * `MeasureOnce()`), the FIFO threshold is above 31 (15 with pressure-and-temperature frames), the
 * decimation above 7, the INT pad drive above 15 (above 7 with push-pull, section 7.5), the
 * out-of-range reference above 17 bits, a timeout is 0 or above `kTimeoutMaxMs`, or normal mode
 * asks for a data rate above the one table 7 allows for the oversampling.
 *
 * @param config The configuration.
 * @return `Error::Ok` or `Error::InvalidArgument`.
 */
constexpr Error Validate(const Config& config) noexcept {
  if (!detail::EnumeratorsValid(config) || (config.mode == PowerMode::Forced) ||
      !detail::RangesValid(config)) {
    return Error::InvalidArgument;
  }
  if ((config.mode == PowerMode::Normal) &&
      (OdrMilliHz(config.odr) >
       MaxOdrMilliHz(config.pressure_oversampling, config.temperature_oversampling))) {
    return Error::InvalidArgument;
  }
  return Error::Ok;
}

/**
 * @brief Measurements on request: standby between `MeasureOnce()` calls, deep standby, the
 * standard-resolution oversampling of table 9 (pressure x4, temperature x1).
 * @return The preset.
 */
constexpr Config ForcedPreset() noexcept {
  Config c{};
  c.pressure_oversampling = Oversampling::X4;
  return c;
}

/**
 * @brief Low power at 1 Hz in normal mode with deep standby between measurements (section 4.3.5).
 * @return The preset.
 */
constexpr Config LowPowerPreset() noexcept {
  Config c{};
  c.mode = PowerMode::Normal;
  c.odr = OutputDataRate::Hz1;
  c.source_data_ready = true;
  return c;
}

/**
 * @brief High resolution at 25 Hz: pressure x16 (table 9), the IIR on the data registers, data
 * ready reported.
 * @return The preset.
 */
constexpr Config HighResolutionPreset() noexcept {
  Config c{};
  c.mode = PowerMode::Normal;
  c.pressure_oversampling = Oversampling::X16;
  c.odr = OutputDataRate::Hz25;
  c.pressure_iir = IirFilter::Coef3;
  c.iir_to_data_registers = true;
  c.source_data_ready = true;
  c.data_ready_timeout_ms = 200;
  return c;
}

/**
 * @brief Pressure and temperature frames into the FIFO at 10 Hz, threshold at 8 frames.
 * @return The preset.
 */
constexpr Config FifoPreset() noexcept {
  Config c{};
  c.mode = PowerMode::Normal;
  c.pressure_oversampling = Oversampling::X8;
  c.odr = OutputDataRate::Hz10;
  c.fifo_frame = FifoFrame::PressureAndTemperature;
  c.fifo_threshold = 8;
  c.source_fifo_threshold = true;
  return c;
}

// Every preset is valid, proved at build time (KB-CPP20-012).
static_assert(Validate(Config{}) == Error::Ok);
static_assert(Validate(ForcedPreset()) == Error::Ok);
static_assert(Validate(LowPowerPreset()) == Error::Ok);
static_assert(Validate(HighResolutionPreset()) == Error::Ok);
static_assert(Validate(FifoPreset()) == Error::Ok);
// Table 7 and section 7.34 at their corners; the conversion times of table 4 with +5 %.
static_assert(MaxOdrMilliHz(Oversampling::X1, Oversampling::X1) == 240'000U);
static_assert(MaxOdrMilliHz(Oversampling::X128, Oversampling::X128) == 5'000U);
static_assert(OdrMilliHz(OutputDataRate::Hz1) == 1'000U);
static_assert(ConversionTimeMaxUs(Oversampling::X1, Oversampling::X1) == 2100U);
static_assert(ConversionTimeMaxUs(Oversampling::X128, Oversampling::X128) == 106'260U);
static_assert(EntersDeepStandby(Config{}) && !EntersDeepStandby(FifoPreset()));
static_assert(ForcedMeasurementMaxUs(Config{}) == 6100U);
// The conversions of section 4.5, exact; the reset value is outside both ranges.
static_assert(SignExtend24(0xFF'FFFFU) == -1);
static_assert(SignExtend24(0x80'0000U) == -8'388'608);
static_assert(SignExtend24(0x7F'FFFFU) == 8'388'607);
static_assert(PressurePa(6'400'000) == 100'000.0F);
static_assert(TemperatureC(-1) == -0.0000152587890625F);
static_assert(!PressureInRange(SignExtend24(0x7F'7F7FU)) &&
              !TemperatureInRange(SignExtend24(0x7F'7F7FU)));

/**
 * @brief Reached only when a `CheckedConfig` is built from an invalid configuration: it is never
 * defined, so the build fails and names it (KB-CPP20-013).
 */
void ConfigIsInvalid() noexcept;

/**
 * @brief A configuration proven valid at build time.
 *
 * `constexpr CheckedConfig kBoard{Config{.mode = PowerMode::Normal}};` fails to compile, naming
 * `ConfigIsInvalid`, when `Validate()` rejects the configuration. Pass `kBoard.Get()` to the
 * driver; the driver still validates at start-up, for configurations built at run time.
 */
class CheckedConfig {
public:
  /**
   * @brief Check `config` at build time.
   * @param config The configuration; it must pass `Validate()`.
   */
  consteval explicit CheckedConfig(const Config& config) noexcept : config_(config) {
    if (Validate(config) != Error::Ok) {
      ConfigIsInvalid();
    }
  }
  /** @return The checked configuration. */
  [[nodiscard]] constexpr const Config& Get() const& noexcept {
    return config_;
  }
  /** @brief Not on a temporary: the reference would dangle (KB-CPP20-044). */
  void Get() const&& = delete;

private:
  Config config_;
};

/** @return The driver version, e.g. `"0.1.0"`, from the generated `bmp581_version.h`. No I/O. */
constexpr const char* GetDriverVersion() noexcept {
  return BMP581_VERSION_STRING;
}
/** @return The major version. No I/O. */
constexpr uint32_t GetDriverVersionMajor() noexcept {
  return BMP581_VERSION_MAJOR;
}
/** @return The minor version. No I/O. */
constexpr uint32_t GetDriverVersionMinor() noexcept {
  return BMP581_VERSION_MINOR;
}
/** @return The patch version. No I/O. */
constexpr uint32_t GetDriverVersionPatch() noexcept {
  return BMP581_VERSION_PATCH;
}

/** @} */

} // namespace bmp581

#endif // BMP581_TYPES_HPP
