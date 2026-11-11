/**
 * @file bmp581.hpp
 * @brief BMP581 driver: the one header to include.
 * @copyright Copyright (c) 2026 The driver-bmp581 authors. SPDX-License-Identifier: MIT
 *
 * @details
 * `bmp581::BMP581<Port>` drives one Bosch BMP581 barometric pressure and temperature sensor through
 * a platform port you write, on I2C (`bmp581_i2c_interface.hpp`) or four-wire SPI
 * (`bmp581_spi_interface.hpp`). The driver has no platform code, allocates nothing, throws
 * nothing, takes no lock and is not thread-safe: give each object one owner, or guard it with one
 * mutex.
 *
 * Section, table and figure numbers refer to the Bosch BMP581 datasheet, document
 * BST-BMP581-DS004-13, revision 1.13.
 */
#ifndef BMP581_HPP
#define BMP581_HPP

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "bmp581_detail.hpp"
#include "bmp581_i2c_interface.hpp"
#include "bmp581_registers.hpp"
#include "bmp581_spi_interface.hpp"
#include "bmp581_types.hpp"
#include "bmp581_version.h"

namespace bmp581 {

/**
 * @brief A port the driver accepts: an `I2cPort` or an `SpiPort`, never both, so the bus framing is
 * never ambiguous.
 * @tparam Port The port class.
 */
template <typename Port>
concept BusPort = (I2cPort<Port> && !SpiPort<Port>) || (SpiPort<Port> && !I2cPort<Port>);

namespace detail {

// Optional hooks: a `Declares*` concept only tests the name, the `Has*` concept the exact
// signature, `noexcept` and the result type; the driver rejects a hook that is declared but does
// not match (KB-CPP20-007).

/// The port declares something called `DelayUs`.
template <typename Port>
concept DeclaresDelayUs = requires(Port& port) { port.DelayUs(1_u32); };

/// The port waits: `void DelayUs(uint32_t us) noexcept`. A narrower parameter would truncate long
/// waits.
template <typename Port>
concept HasDelayUs = requires { ExactHook<Port, void(uint32_t) noexcept>::Take(&Port::DelayUs); };

/// The port declares something called `Log`.
template <typename Port>
concept DeclaresLog = requires(Port& port) { port.Log(LogLevel::Info, ""); };

/// The port takes log lines: `void Log(LogLevel, const char* message) noexcept`.
template <typename Port>
concept HasLog =
    requires { ExactHook<Port, void(LogLevel, const char*) noexcept>::Take(&Port::Log); };

/// The register framing of a port's bus: `I2cRegisterBus` or `SpiRegisterBus`.
template <BusPort Port>
struct BusFor;
/// I2C framing.
template <BusPort Port>
  requires I2cPort<Port>
struct BusFor<Port> {
  using Type = I2cRegisterBus<Port>; ///< The framing.
  /// Whether the first frame after a reset can carry no data: never on I2C (section 5.1).
  static constexpr bool kSelectionFrame = false;
};
/// SPI framing.
template <BusPort Port>
  requires SpiPort<Port>
struct BusFor<Port> {
  using Type = SpiRegisterBus<Port>; ///< The framing.
  /// Whether the first frame after a reset can carry no data: on SPI it only selects SPI again,
  /// and its bytes are whatever MISO shows (section 5.1).
  static constexpr bool kSelectionFrame = true;
};

/// Wait through the port. A port without `DelayUs()` fails to compile at the first call that needs
/// a wait: the chip's reset and conversion times are requirements, never skipped.
template <typename Port>
void DelayUs(Port& port, uint32_t us) noexcept {
  if constexpr (HasDelayUs<Port>) {
    port.DelayUs(us);
  } else {
    static_assert(kAlwaysFalse<Port>,
                  "bmp581: the port must implement `void DelayUs(uint32_t us) noexcept`");
  }
}

/// Log through the port, or nothing when the port has no `Log()`. The driver never formats text.
template <typename Port>
void Log(Port& port, [[maybe_unused]] LogLevel level,
         [[maybe_unused]] const char* message) noexcept {
  if constexpr (HasLog<Port>) {
    port.Log(level, message);
  } else {
    (void)port;
  }
}

// Register encoders and decoders: free constexpr functions, emitted once whatever the port type,
// and proved below with static_assert (KB-CPP20-009, -012).

/// `value` with `bit` set when `on`.
[[nodiscard]] constexpr uint8_t SetIf(uint8_t value, uint8_t bit, bool on) noexcept {
  return on ? static_cast<uint8_t>(static_cast<uint32_t>(value) | static_cast<uint32_t>(bit))
            : value;
}

/// Whether `value` has any bit of `bits` set.
[[nodiscard]] constexpr bool Has(uint8_t value, uint8_t bits) noexcept {
  return (static_cast<uint32_t>(value) & static_cast<uint32_t>(bits)) != 0U;
}

/// INT_CONFIG for a configuration (section 7.5).
[[nodiscard]] constexpr uint8_t IntConfigValue(const Config& c) noexcept {
  auto v = reg::IntConfigMode::Insert(0_u8, ToUnderlying(c.int_mode));
  v = reg::IntConfigPol::Insert(v, ToUnderlying(c.int_polarity));
  v = reg::IntConfigOd::Insert(v, ToUnderlying(c.int_drive));
  v = reg::IntConfigEn::Insert(v, c.int_pin_enable ? 1_u32 : 0_u32);
  return reg::IntConfigPadDrv::Insert(v, c.int_pad_drive);
}

/// INT_SOURCE for a configuration (section 7.6).
[[nodiscard]] constexpr uint8_t IntSourceValue(const Config& c) noexcept {
  auto v = SetIf(0_u8, reg::kIntDrdy, c.source_data_ready);
  v = SetIf(v, reg::kIntFifoFull, c.source_fifo_full);
  v = SetIf(v, reg::kIntFifoThs, c.source_fifo_threshold);
  return SetIf(v, reg::kIntOorP, c.source_out_of_range);
}

/// FIFO_CONFIG for a configuration (section 7.7).
[[nodiscard]] constexpr uint8_t FifoConfigValue(const Config& c) noexcept {
  return reg::FifoModeBit::Insert(reg::FifoThreshold::Insert(0_u8, c.fifo_threshold),
                                  ToUnderlying(c.fifo_mode));
}

/// FIFO_SEL for a configuration (section 7.9).
[[nodiscard]] constexpr uint8_t FifoSelValue(const Config& c) noexcept {
  return reg::FifoDecSel::Insert(reg::FifoFrameSel::Insert(0_u8, ToUnderlying(c.fifo_frame)),
                                 c.fifo_decimation);
}

/// DSP_CONFIG for a configuration (section 7.27): both values always compensated (comp_pt_en =
/// 0b11, its reset value), the IIR routing and the forced-mode flush from `Config`.
[[nodiscard]] constexpr uint8_t DspConfigValue(const Config& c) noexcept {
  auto v = reg::DspCompPtEn::Insert(0_u8, 3_u32);
  v = SetIf(v, reg::kDspIirFlushForced, c.iir_flush_in_forced);
  v = SetIf(v, reg::kDspShdwSelIir, c.iir_to_data_registers);
  v = SetIf(v, reg::kDspFifoSelIir, c.iir_to_fifo);
  return SetIf(v, reg::kDspOorSelIirP, c.iir_to_out_of_range);
}

/// DSP_IIR for a configuration (section 7.28).
[[nodiscard]] constexpr uint8_t DspIirValue(const Config& c) noexcept {
  return reg::DspIirP::Insert(reg::DspIirT::Insert(0_u8, ToUnderlying(c.temperature_iir)),
                              ToUnderlying(c.pressure_iir));
}

/// OOR_CONFIG for a configuration (section 7.32): reference bit 16 and the count limit.
[[nodiscard]] constexpr uint8_t OorConfigValue(const Config& c) noexcept {
  return reg::OorCntLim::Insert(reg::OorThrP16::Insert(0_u8, c.oor_reference_pa >> 16U),
                                ToUnderlying(c.oor_count));
}

/// OSR_CONFIG for a configuration (section 7.33). press_en is always 1: the driver measures
/// pressure, and OSR_CONFIG reads 0x00 only after a reset, which makes press_en the reset sentinel
/// that no write of the driver erases.
[[nodiscard]] constexpr uint8_t OsrConfigValue(const Config& c) noexcept {
  const auto v =
      reg::OsrP::Insert(reg::OsrT::Insert(0_u8, ToUnderlying(c.temperature_oversampling)),
                        ToUnderlying(c.pressure_oversampling));
  return SetIf(v, reg::kOsrPressEn, true);
}

/// ODR_CONFIG for a configuration and a power mode (section 7.34).
[[nodiscard]] constexpr uint8_t OdrConfigValue(const Config& c, PowerMode mode) noexcept {
  const auto v =
      reg::OdrPwrMode::Insert(reg::OdrOdr::Insert(0_u8, ToUnderlying(c.odr)), ToUnderlying(mode));
  return SetIf(v, reg::kOdrDeepDis, !c.deep_standby);
}

/// One register write of the configuration sequence.
struct RegisterWrite {
  uint8_t reg;   ///< The address.
  uint8_t value; ///< The byte.
};

/// The configuration writes in the order the driver sends them, in standby with deep standby off
/// (section 4.3.1: only standby takes every write), after INT_SOURCE = 0 and an INT_STATUS read
/// (section 4.7.3.2: an INT mode change needs both first). INT_SOURCE comes after INT_CONFIG, and
/// ODR_CONFIG last: it may enable deep standby, which need not take the other writes (4.3.2).
[[nodiscard]] constexpr std::array<RegisterWrite, 12> ConfigWrites(const Config& c) noexcept {
  return {
      {{.reg = reg::kIntConfig, .value = IntConfigValue(c)},
       {.reg = reg::kDspConfig, .value = DspConfigValue(c)},
       {.reg = reg::kDspIir, .value = DspIirValue(c)},
       {.reg = reg::kFifoConfig, .value = FifoConfigValue(c)},
       {.reg = reg::kFifoSel, .value = FifoSelValue(c)},
       {.reg = reg::kOorThrPLsb, .value = static_cast<uint8_t>(c.oor_reference_pa & 0xFFU)},
       {.reg = reg::kOorThrPMsb, .value = static_cast<uint8_t>((c.oor_reference_pa >> 8U) & 0xFFU)},
       {.reg = reg::kOorRange, .value = c.oor_window_pa},
       {.reg = reg::kOorConfig, .value = OorConfigValue(c)},
       {.reg = reg::kOsrConfig, .value = OsrConfigValue(c)},
       {.reg = reg::kIntSource, .value = IntSourceValue(c)},
       {.reg = reg::kOdrConfig, .value = OdrConfigValue(c, PowerMode::Standby)}}};
}

/// INT_CONFIG to FIFO_SEL (0x14 to 0x18) as a read-back of the configuration shows them; FIFO_COUNT
/// is not compared, only its reserved bits.
[[nodiscard]] constexpr std::array<uint8_t, 5> ExpectedBlockA(const Config& c) noexcept {
  return {IntConfigValue(c), IntSourceValue(c), FifoConfigValue(c), 0_u8, FifoSelValue(c)};
}

/// DSP_CONFIG to ODR_CONFIG (0x30 to 0x37) in standby, as a read-back shows them.
[[nodiscard]] constexpr std::array<uint8_t, 8> ExpectedBlockB(const Config& c) noexcept {
  return {DspConfigValue(c),
          DspIirValue(c),
          static_cast<uint8_t>(c.oor_reference_pa & 0xFFU),
          static_cast<uint8_t>((c.oor_reference_pa >> 8U) & 0xFFU),
          c.oor_window_pa,
          OorConfigValue(c),
          OsrConfigValue(c),
          OdrConfigValue(c, PowerMode::Standby)};
}

/// ODR_CONFIG for standby with deep standby disabled: the state in which every register takes a
/// write (sections 4.3.1, 4.3.2).
[[nodiscard]] constexpr uint8_t OdrAwakeStandby(const Config& c) noexcept {
  return SetIf(OdrConfigValue(c, PowerMode::Standby), reg::kOdrDeepDis, true);
}

/// The marker in NVM_DATA_LSB and NVM_DATA_MSB: both reset to 0x00 (sections 7.25, 7.26) and do
/// nothing without an NVM command in CMD (section 4.8.1). Before each configuration sequence, in
/// standby, the driver reads them and writes the marker unless they hold it; the sequence itself
/// does not write them, so a reset during it shows as 0x00 0x00 at the read-back. An NVM download
/// (a measurement from deep standby, section 7.22) may overwrite them with any value, 0x00 0x00
/// included, so the marker counts only while no such measurement has run since.
inline constexpr std::array<uint8_t, 2> kMarker{0x3CU, 0xC3U};

/// Whether OSR_CONFIG shows a reset: press_en = 0, which the driver never writes (section 7.33).
[[nodiscard]] constexpr bool OsrShowsReset(uint8_t osr) noexcept {
  return !Has(osr, reg::kOsrPressEn);
}

/// INT_STATUS, decoded (section 7.21).
[[nodiscard]] constexpr InterruptStatus DecodeIntStatus(uint8_t raw) noexcept {
  return InterruptStatus{.data_ready = Has(raw, reg::kIntDrdy),
                         .fifo_full = Has(raw, reg::kIntFifoFull),
                         .fifo_threshold = Has(raw, reg::kIntFifoThs),
                         .out_of_range = Has(raw, reg::kIntOorP),
                         .power_on_reset = Has(raw, reg::kIntPor)};
}

/// A 24-bit little-endian value from three bytes, XLSB first (sections 7.11 to 7.16).
[[nodiscard]] constexpr uint32_t Le24(std::span<const uint8_t, 3> b) noexcept {
  return static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8U) |
         (static_cast<uint32_t>(b[2]) << 16U);
}

/// Bytes of the data read: TEMP_DATA to STATUS, 0x1D to 0x28 (section 7).
inline constexpr std::size_t kDataFrameBytes = 12U;
/// Offset of INT_STATUS in the data read.
inline constexpr std::size_t kDataIntStatus = 10U;
/// Offset of STATUS in the data read.
inline constexpr std::size_t kDataStatus = 11U;

/// Whether every byte of `bytes` is `value`.
[[nodiscard]] constexpr bool AllBytes(std::span<const uint8_t> bytes, uint8_t value) noexcept {
  return std::ranges::all_of(bytes, [value](uint8_t b) noexcept -> bool { return b == value; });
}

/// Whether the data read can come from the chip: the reserved registers 0x23 to 0x26 read 0x00
/// (sections 7.17 to 7.20), the reserved bits of INT_STATUS and STATUS read 0, and the frame is
/// not all 0x00 (SDA or MISO stuck low; a real frame has a pressure of at least 30 kPa). A line
/// floating high fails the reserved registers.
[[nodiscard]] constexpr bool DataFrameValid(std::span<const uint8_t, kDataFrameBytes> f) noexcept {
  const auto reserved = f.subspan<6, 4>();
  return AllBytes(reserved, 0_u8) && !Has(f[kDataIntStatus], 0xE0_u8) &&
         !Has(f[kDataStatus], reg::kStatusReserved) && !AllBytes(f, 0_u8);
}

/// A temperature and a pressure as the chip stores them: `NoData` for the reset value 0x7F7F7F,
/// `OutOfRange` outside tables 1 and 2.
[[nodiscard]] constexpr Result<Measurement> DecodeMeasurement(uint32_t temperature,
                                                              uint32_t pressure) noexcept {
  if ((temperature == reg::kDataReset) || (pressure == reg::kDataReset)) {
    return Result<Measurement>::Failure(Error::NoData);
  }
  const Measurement m{.pressure_raw = SignExtend24(pressure),
                      .temperature_raw = SignExtend24(temperature)};
  if (!PressureInRange(m.pressure_raw) || !TemperatureInRange(m.temperature_raw)) {
    return Result<Measurement>::Failure(Error::OutOfRange);
  }
  return Result<Measurement>::Success(m);
}

/// Bytes of one FIFO frame of a frame type: 3, or 6 with pressure and temperature (tables 11 to
/// 13); 0 when the FIFO is off.
[[nodiscard]] constexpr uint32_t FifoFrameBytes(FifoFrame frame) noexcept {
  switch (frame) {
  case FifoFrame::Temperature:
  case FifoFrame::Pressure:
    return 3U;
  case FifoFrame::PressureAndTemperature:
    return 6U;
  case FifoFrame::Disabled:
  default:
    break;
  }
  return 0U;
}

/// Frames the FIFO holds for a frame type (section 4.6).
[[nodiscard]] constexpr uint32_t FifoCapacity(FifoFrame frame) noexcept {
  return (frame == FifoFrame::PressureAndTemperature) ? kFifoFramesBoth : kFifoFramesSingle;
}

/// One value of a FIFO frame into a sample: `NoData` for the empty frame 0x7F7F7F (section 4.6.3),
/// `OutOfRange` outside the measurement range.
[[nodiscard]] constexpr FifoSample DecodeFifoValue(FifoSample s, uint32_t raw,
                                                   bool pressure) noexcept {
  if (s.error != Error::Ok) {
    return s;
  }
  if (raw == reg::kDataReset) {
    return FifoSample{.error = Error::NoData};
  }
  const int32_t value = SignExtend24(raw);
  if (pressure ? !PressureInRange(value) : !TemperatureInRange(value)) {
    return FifoSample{.error = Error::OutOfRange};
  }
  if (pressure) {
    s.has_pressure = true;
    s.pressure_raw = value;
  } else {
    s.has_temperature = true;
    s.temperature_raw = value;
  }
  return s;
}

/// One FIFO frame of a frame type: temperature first in a PT frame (table 11).
[[nodiscard]] constexpr FifoSample DecodeFifoFrame(FifoFrame frame,
                                                   std::span<const uint8_t, 6> b) noexcept {
  const uint32_t first = Le24(b.first<3>());
  if (frame == FifoFrame::PressureAndTemperature) {
    const FifoSample t = DecodeFifoValue(FifoSample{}, first, false);
    return DecodeFifoValue(t, Le24(b.last<3>()), true);
  }
  return DecodeFifoValue(FifoSample{}, first, frame == FifoFrame::Pressure);
}

// The reset configuration encodes to the reset values of section 7, except press_en.
static_assert(IntConfigValue(Config{}) == reg::kIntConfigReset);
static_assert(DspConfigValue(Config{}) == reg::kDspConfigReset);
static_assert(OdrConfigValue(Config{}, PowerMode::Standby) == reg::kOdrConfigReset);
static_assert(OsrConfigValue(Config{}) == reg::kOsrPressEn);
static_assert((IntSourceValue(Config{}) == 0U) && (FifoConfigValue(Config{}) == 0U) &&
              (FifoSelValue(Config{}) == 0U) && (DspIirValue(Config{}) == 0U) &&
              (OorConfigValue(Config{}) == 0U));
// No configuration encodes OSR_CONFIG without press_en; the reset value shows a reset.
static_assert(!OsrShowsReset(OsrConfigValue(HighResolutionPreset())));
static_assert(OsrShowsReset(reg::kOsrConfigReset));
// The example of section 4.7.2.3: 97150 Pa +/- 50 Pa.
static_assert(OorConfigValue(Config{.oor_reference_pa = 97'150U}) == 0x01U);
static_assert(ConfigWrites(Config{.oor_reference_pa = 97'150U})[6].value == 0x7BU);
static_assert(ConfigWrites(Config{.oor_reference_pa = 97'150U})[5].value == 0x7EU);
// The FIFO frames of tables 11 to 13 and the empty frame of table 14.
static_assert(Le24(std::array<uint8_t, 3>{0x00U, 0xA6U, 0x0EU}) == 0x0E'A600U);
static_assert(DecodeFifoFrame(FifoFrame::Pressure,
                              std::array<uint8_t, 6>{0x00U, 0xA6U, 0x5EU, 0U, 0U, 0U})
                  .pressure_raw == 6'202'880);
static_assert(DecodeFifoFrame(FifoFrame::Temperature,
                              std::array<uint8_t, 6>{0x7FU, 0x7FU, 0x7FU, 0U, 0U, 0U})
                  .error == Error::NoData);
static_assert(DecodeMeasurement(0x7F'7F7FU, 0x7F'7F7FU).error == Error::NoData);
static_assert(DecodeMeasurement(0x19'0000U, 0x61'A800U).value.pressure_raw == 6'400'000);
static_assert(DecodeMeasurement(0U, 0U).error == Error::OutOfRange);
static_assert(DecodeIntStatus(reg::kIntStatusAfterReset).power_on_reset);

} // namespace detail

/**
 * @defgroup bmp581_driver BMP581 driver
 * @brief The driver class.
 * @{
 */

// clang-format off: the table below must keep one row per line.
/**
 * @brief Driver for one BMP581 on a platform port.
 *
 * Lifecycle: construct with the configuration (no I/O), then `EnsureInitialized()`, the one way to
 * start (soft reset, identity, NVM ready, configuration), use, optionally `Deinitialize()` (soft
 * reset: deep standby). Every call that touches the chip returns `Error::NotInitialized` until
 * `EnsureInitialized()` has succeeded; no call starts the chip as a side effect.
 *
 * **Values.** A measurement is one 12-byte read of TEMP_DATA to STATUS (sections 4.5.1, 5.4), so
 * temperature and pressure come from the same conversion, the reserved registers in the middle
 * prove the line, and INT_STATUS comes for free. No all-0x00 or all-0xFF frame, no reset value
 * (0x7F7F7F) and nothing outside the measurement range is ever returned as a value.
 *
 * **Resets.** A power-on reset (brown-out) or a soft reset loads every register with its reset
 * value and sets INT_STATUS.por (sections 4.7.2.4, 7.21). The driver always writes OSR_CONFIG with
 * press_en = 1, so OSR_CONFIG.press_en = 0 is the reset sentinel, and no write of the driver erases
 * it; a marker in NVM_DATA, written at start-up, shows a reset that a configuration sequence's own
 * OSR_CONFIG write covered. Every setter reads INT_STATUS, OSR_CONFIG and ODR_CONFIG first and
 * writes nothing over a reset chip; every INT_STATUS read (data reads, polls, raw reads) is checked
 * for por. Evidence is confirmed by a second read before the driver acts: it is then uninitialised
 * and returns `DeviceReset`. A check that is due but cannot be read stays pending, and no call
 * returns `Ok` until it has passed.
 *
 * **Writes.** Every write is one register in one transfer, stored whole or not at all. The
 * configuration is written in standby with deep standby off (sections 4.3.1, 4.3.8), ODR_CONFIG
 * last, and read back in full; a power mode change
 * goes through standby (section 4.3.7) and is read back. A failed or mismatching standby or mode
 * write leaves the configuration in doubt (`IsConfigKnown()`): calls that depend on it return
 * `StateUnknown` until `ApplyConfig()` or `EnsureInitialized()` succeeds. A configuration sequence
 * that fails after its first write leaves the driver uninitialised. `SetPowerMode(PowerMode::Standby)` and
 * `Deinitialize()` are never refused once the driver has started.
 *
 * Port hooks besides the bus hooks, found at compile time:
 *
 * | Hook | Used for | Without it |
 * | --- | --- | --- |
 * | `void DelayUs(uint32_t us) noexcept` | t_powup, t_soft_res, t_standby and the polls | Compile error: the waits are required |
 * | `bool GetLastFailure(I2cFailure& failure) noexcept` (I2C) | Telling an address NACK (`NoDevice`) from other failures | Every failed transfer is `Bus` |
 * | `void Log(LogLevel, const char* message) noexcept` | One line per lifecycle event or failure | Nothing is logged |
 *
 * A hook declared with the wrong signature (for example `void DelayUs(uint16_t)`) is a compile
 * error.
 *
 * @tparam Port Your class derived from `I2cInterface<Port>` or `SpiInterface<Port>`.
 */
// clang-format on
template <BusPort Port>
class BMP581 {
  static_assert(!detail::DeclaresDelayUs<Port> || detail::HasDelayUs<Port>,
                "bmp581: the DelayUs hook must be `void DelayUs(uint32_t us) noexcept`, so that no "
                "wait is truncated");
  static_assert(!detail::DeclaresLog<Port> || detail::HasLog<Port>,
                "bmp581: the optional Log hook must be `void Log(LogLevel, const char* message) "
                "noexcept`");
  static_assert(!detail::DeclaresGetLastFailure<Port> || detail::HasGetLastFailure<Port>,
                "bmp581: the optional GetLastFailure hook must be `bool "
                "GetLastFailure(I2cFailure& failure) noexcept`");

public:
  /**
   * @brief Bind the driver to a port (on I2C at `i2c::kI2cAddress`) and store the configuration.
   * No I/O, cannot fail: `EnsureInitialized()` checks the configuration.
   * @param port The port; it must outlive the driver.
   * @param config What `EnsureInitialized()` will apply.
   */
  explicit BMP581(Port& port, const Config& config = {}) noexcept : bus_(port), config_(config) {}

  /**
   * @brief Bind the driver to an I2C port at a 7-bit address and store the configuration. No I/O.
   * @param port The port; it must outlive the driver.
   * @param address `i2c::kI2cAddress` (SDO low) or `i2c::kI2cAddressSdoHigh`; checked by
   *        `EnsureInitialized()`.
   * @param config What `EnsureInitialized()` will apply.
   */
  BMP581(Port& port, uint8_t address, const Config& config = {}) noexcept
    requires I2cPort<Port>
      : bus_(port, address), config_(config) {}

  /** @brief One driver object per chip. */
  BMP581(const BMP581&) = delete;
  /** @brief One driver object per chip. */
  BMP581& operator=(const BMP581&) = delete;

  /// @name Lifecycle
  /// @{

  /**
   * @brief Start the chip. Once started, a later call runs the reset check (one 3-byte read) and
   * returns `Ok`, or starts again when the chip reset or the configuration is in doubt.
   *
   * Checks the configuration and the address (no I/O when either is bad), brings the bus up, then:
   *
   * 1. waits t_powup (2 ms, table 4); on SPI sends the dummy read that selects SPI (section 5.1);
   * 2. soft reset: CMD = 0xB6 (over I2C the chip NACKs it by design, section 7.36), waits
   *    t_soft_res (2 ms), and on SPI selects SPI again;
   * 3. the post-power-up checks of section 4.3.9: CHIP_ID = 0x50 (`DeviceIdMismatch`), STATUS
   *    polled every `kPollUs` until status_nvm_rdy = 1, at most `Config::startup_timeout_ms`
   *    (`Timeout`), with status_nvm_err and status_nvm_cmd_err clear (`DeviceFault`), and
   *    INT_STATUS = 0x10 (the reset happened) then 0x00 (the read cleared it): a stuck, floating or
   *    looped line cannot pass both;
   * 4. leaves the deep standby the reset left the chip in (standby with deep_dis = 1, t_standby:
   *    only standby takes every write, section 4.3.1), writes the reset marker to NVM_DATA, writes
   *    the configuration with ODR_CONFIG last, reads all of it and the marker back
   * (`WriteMismatch`, `DeviceReset`) and enters the configured mode, read back too
   * (`ConfigRejected` when normal mode's data rate is still invalid at the deadline). With the FIFO
   * on, it waits t_reconf for the FIFO flush (section 4.6.4).
   *
   * About 7 ms and 26 transfers. After a failure the driver stays uninitialised and the next call
   * starts again at step 1.
   *
   * @return `Ok`, `InvalidArgument`, `NoDevice`, `Bus`, `DeviceIdMismatch`, `Timeout`,
   *         `DeviceFault`, `InvalidReply`, `WriteMismatch`, `ConfigRejected` or `DeviceReset` (the
   *         chip reset during the sequence).
   */
  Result<void> EnsureInitialized() noexcept;

  /** @return true after a successful `EnsureInitialized()`, until `Deinitialize()` or a found
   * reset. No I/O. */
  [[nodiscard]] bool IsInitialized() const noexcept {
    return initialized_;
  }

  /**
   * @brief Put the chip in its lowest-power state with the soft reset, CMD = 0xB6: it leaves the
   * chip in deep standby with every register at its reset value (section 4.3). Marks the driver
   * uninitialised.
   *
   * Always does its work, whatever the driver believes (never started, started, a reset found, the
   * configuration in doubt): brings the bus up, selects SPI on SPI, writes ODR_CONFIG = standby
   * with deep_dis = 1 and waits t_standby (the datasheet does not say that deep standby takes CMD;
   * the result of this write is not needed), writes CMD, waits t_soft_res,
   * and reads INT_STATUS, which must show por: `Ok` only when the reset is proved. An address that
   * `EnsureInitialized()` refuses is `InvalidArgument` with no I/O.
   *
   * @return `Ok`, `InvalidArgument`, `NoDevice`, `Bus` or `WriteMismatch` (INT_STATUS did not show
   *         the reset).
   */
  Result<void> Deinitialize() noexcept;

  /**
   * @brief Replace the configuration.
   *
   * Uninitialised: `config` is only stored, for `EnsureInitialized()`, with no I/O. Initialised:
   * the reset check, standby (when the chip measures: ODR_CONFIG, t_standby, a read that confirms
   * it), INT_SOURCE = 0 and an INT_STATUS read, the twelve configuration writes, a full read-back,
   * a last INT_STATUS read, then the configured mode. About 20 transfers.
   *
   * @param config Checked with `Validate()` before any I/O.
   * @return `Ok`, `InvalidArgument` (nothing stored or sent), `NoDevice`, `Bus`, `InvalidReply`,
   *         `Timeout`, `WriteMismatch`, `ConfigRejected` or `DeviceReset`. After a failure that
   *         wrote, `IsConfigKnown()` is false; after a failure among the configuration writes the
   *         driver is uninitialised (a reset could hide behind them): call `EnsureInitialized()`.
   */
  Result<void> ApplyConfig(const Config& config) noexcept;

  /** @return The configuration stored or last applied; `mode` follows `SetPowerMode()`. No I/O. */
  [[nodiscard]] const Config& GetConfig() const& noexcept {
    return config_;
  }
  /** @brief Not on a temporary driver: the reference would dangle (KB-CPP20-044). */
  void GetConfig() const&& = delete;

  /** @return true when the chip holds `GetConfig()`, as last written and read back; false after a
   * failed or mismatching write until `ApplyConfig()` or `EnsureInitialized()` succeeds. No I/O. */
  [[nodiscard]] bool IsConfigKnown() const noexcept {
    return config_known_;
  }

  /**
   * @brief Change the power mode (section 4.3.7).
   *
   * `Standby`: never refused once the driver has started, also when it is uninitialised after a
   * reset or the configuration is in doubt, and after a failed first start that reached the chip:
   * ODR_CONFIG with pwr_mode = 0, t_standby (2.5 ms), then a read that confirms pwr_mode = 0.
   * `Normal` and `Continuous`: the reset check, standby first when the chip measures, ODR_CONFIG,
   * then reads of OSR_CONFIG to OSR_EFF every `kPollUs` until the mode and, in normal mode,
   * OSR_EFF.odr_is_valid show, at most t_startup_deep (`ConfigRejected` only then). With the FIFO
   * on, every mode waits t_reconf for the FIFO flush before `Ok` (section 4.6.4).
   *
   * @param mode `Standby`, `Normal` or `Continuous`; `Forced` is `MeasureOnce()`.
   * @return `Ok`, `NotInitialized`, `InvalidArgument` (`Forced`, not an enumerator, or a data rate
   *         normal mode cannot reach with the oversampling; nothing sent), `StateUnknown`,
   *         `NoDevice`, `Bus`, `InvalidReply`, `Timeout` (standby not reached), `WriteMismatch`,
   *         `ConfigRejected` or `DeviceReset`.
   */
  Result<void> SetPowerMode(PowerMode mode) noexcept;

  /**
   * @brief Find out whether the chip reset since the start: one 3-byte read of OSR_CONFIG to
   * OSR_EFF, a second one only to confirm reset evidence. Also settles a pending check.
   *
   * Call it where a supply dip is possible and the application only reads the FIFO: data reads
   * check INT_STATUS by themselves.
   *
   * @return `Ok`, `NotInitialized`, `NoDevice`, `Bus`, `InvalidReply` (a reserved bit, or an
   *         INT_STATUS.por that the registers contradict) or `DeviceReset`.
   */
  Result<void> CheckForReset() noexcept;

  /// @}
  /// @name Measurement
  /// @{

  /**
   * @brief Read the latest measurement from the data registers: one 12-byte read of TEMP_DATA to
   * STATUS (sections 4.5, 4.5.1), whatever the power mode (in standby, the last one measured).
   *
   * INT_STATUS in the same read goes to the latch (`GetLatchedInterrupts()`); a por in it runs the
   * reset check first.
   *
   * @param verify `Verify::ReadBack` to run the reset check too (one more 3-byte read) and read the
   *        data registers a second time (6 bytes; a third read when they differ): the bus has no
   *        CRC, and a bit flipped inside a data byte is not detectable without a second read. In
   *        continuous mode on a slow bus, or with a pre-empted caller, two new measurements inside
   *        the three reads give a false `InvalidReply`, never a wrong value.
   * @return The measurement, or `NotInitialized`, `InvalidArgument` (`verify` not an enumerator;
   *         nothing sent), `NoDevice`, `Bus`, `InvalidReply` (the frame cannot come from the chip,
   *         or three reads of the data differ), `DeviceReset`, `NoData` (no measurement since the
   *         reset) or `OutOfRange`.
   */
  Result<Measurement> ReadMeasurement(Verify verify = Verify::None) noexcept;

  /**
   * @brief One forced measurement (section 4.3.3): the reset check, ODR_CONFIG with pwr_mode =
   * forced, polled until it shows forced, at most t_startup (`WriteMismatch`: the write was lost),
   * a wait of the typical conversion time less 5 %, then polls of OSR_CONFIG to OSR_EFF
   * every `kPollUs` until the chip is back in standby, at most `ForcedMeasurementMaxUs()`
   * (`Timeout`), then the read of `ReadMeasurement()`.
   *
   * @param verify `Verify::ReadBack` to read the data registers a second time, as
   *        `ReadMeasurement()` does.
   * @return The measurement, or `NotInitialized`, `InvalidArgument` (`verify` not an enumerator;
   *         nothing sent), `StateUnknown`, `Busy` (the configured mode is not standby, or the chip
   *         still measures; nothing sent), `NoDevice`, `Bus`, `InvalidReply`, `Timeout`,
   *         `DeviceReset`, `NoData` or `OutOfRange`.
   */
  Result<Measurement> MeasureOnce(Verify verify = Verify::None) noexcept;

  /**
   * @brief Wait for new data in normal or continuous mode: INT_STATUS polled every `kPollUs` until
   * drdy_data_reg, at most `Config::data_ready_timeout_ms`. Each read clears the flags on the chip
   * and feeds the latch. On SPI the reset check runs before `Ok` (one 3-byte read): the frame after
   * a reset only selects SPI and may read like data (section 5.1).
   *
   * @return `Ok`, `NotInitialized`, `Unsupported` (no data-ready source in `Config`, or standby,
   *         where no measurement comes; nothing sent), `StateUnknown`, `NoDevice`, `Bus`,
   *         `InvalidReply`, `Timeout` or `DeviceReset`.
   */
  Result<void> WaitDataReady() noexcept;

  /**
   * @brief Read INT_STATUS once (section 7.21): clears its flags on the chip; the driver latches
   * them. On SPI the reset check runs before it returns (one 3-byte read): the frame after a reset
   * only selects SPI and may read like data (section 5.1).
   * @return The flags, or `NotInitialized`, `NoDevice`, `Bus`, `InvalidReply` or `DeviceReset`.
   */
  Result<InterruptStatus> ReadInterruptStatus() noexcept;

  /// @}
  /// @name FIFO
  /// @{

  /**
   * @brief Read FIFO_COUNT (section 4.6.2). With the FIFO on, a count of 0 runs the reset check: a
   * reset empties and disables the FIFO. On SPI the reset check runs before it returns (one 3-byte
   * read): the frame after a reset only selects SPI and may read like data (section 5.1).
   * @return The frames waiting, or `NotInitialized`, `StateUnknown`, `NoDevice`, `Bus`,
   *         `InvalidReply` (a reserved bit, or more frames than the FIFO holds) or `DeviceReset`.
   */
  Result<uint8_t> ReadFifoCount() noexcept;

  /**
   * @brief Read up to `out.size()` frames from the FIFO: FIFO_COUNT, then one burst of whole frames
   * from FIFO_DATA (section 4.6.3). Each sample carries its own validity (`NoData` for the empty
   * frame, `OutOfRange`). A burst of all 0x00 or all 0xFF is `InvalidReply` (a stuck line; the
   * frames read are lost). An empty FIFO, an empty frame or a frame out of range runs the reset
   * check, since a reset empties the FIFO. On SPI the reset check always runs before the samples
   * return (one 3-byte read): a reset between FIFO_COUNT and FIFO_DATA turns the burst into the
   * frame that only selects SPI (section 5.1).
   *
   * @param out Where the samples go; at least one.
   * @param verify `Verify::ReadBack` to run the reset check also when frames were read.
   * @return The number of samples written, or `NotInitialized`, `Unsupported` (the FIFO is off in
   *         `Config`), `InvalidArgument` (empty `out`), `StateUnknown`, `NoDevice`, `Bus`,
   *         `InvalidReply` or `DeviceReset`.
   */
  Result<std::size_t> ReadFifo(std::span<FifoSample> out, Verify verify = Verify::None) noexcept;

  /// @}
  /// @name Diagnostics
  /// @{

  /**
   * @brief The INT_STATUS flags seen by any read since `ClearLatchedInterrupts()`, the driver's own
   * reads included (handbook KB-CPP-119). No I/O. The start-up empties it: the por of the driver's
   * own soft reset is not news.
   * @return The latched flags.
   */
  [[nodiscard]] InterruptStatus GetLatchedInterrupts() const noexcept {
    return detail::DecodeIntStatus(latched_int_);
  }

  /** @brief Empty the latch of `GetLatchedInterrupts()`. No I/O. */
  void ClearLatchedInterrupts() noexcept {
    latched_int_ = 0;
  }

  /** @return INT_STATUS as last read, by any call; 0 before the first. No I/O (KB-CPP-129). */
  [[nodiscard]] uint8_t GetLastIntStatus() const noexcept {
    return last_int_status_;
  }

  /** @return STATUS as last read, by any call; 0 before the first. No I/O. */
  [[nodiscard]] uint8_t GetLastStatus() const noexcept {
    return last_status_;
  }

  /** @return OSR_EFF as last read (by a change to normal or continuous mode); 0 before. No I/O. */
  [[nodiscard]] uint8_t GetLastOsrEff() const noexcept {
    return last_osr_eff_;
  }

  /** @return REV_ID read by the last start; 0 before. No I/O. */
  [[nodiscard]] uint8_t GetRevision() const noexcept {
    return revision_;
  }

  /**
   * @brief Read one register once: one transfer, no retry, no decoding (handbook KB-CPP-129).
   *
   * For bring-up and diagnostics. Reading INT_STATUS clears it on the chip (the driver latches it,
   * and a por in it runs the reset check before the value is returned); reading CHIP_STATUS clears
   * its I3C error flags. FIFO_DATA (it would consume part of a frame) and CMD are refused. On SPI
   * the reset check runs before it returns (one 3-byte read): the frame after a reset only selects
   * SPI and may read like data (section 5.1).
   *
   * @param address 0x01, 0x02, 0x11, 0x13 to 0x18, 0x1C to 0x28, 0x2B to 0x2D or 0x30 to 0x38.
   * @return The register value, or `NotInitialized`, `InvalidArgument` (nothing sent), `NoDevice`,
   *         `Bus`, `InvalidReply` or `DeviceReset`.
   */
  Result<uint8_t> ReadRegister(uint8_t address) noexcept;

  /// @}

private:
  using Bus = typename detail::BusFor<Port>::Type;

  Result<void> initialize() noexcept;
  Result<void> resetChip() noexcept;
  Result<void> identify() noexcept;
  Result<void> waitNvmReady() noexcept;
  Result<void> checkResetFlags() noexcept;
  Result<void> wake(const Config& config) noexcept;
  void leaveDeepStandby() noexcept;
  Result<void> configure(const Config& config, uint8_t odr) noexcept;
  void waitFifoFlush() noexcept;
  Result<bool> markerCleared() noexcept;
  Result<void> ensureMarker() noexcept;
  Result<void> waitForcedShown() noexcept;
  Result<void> checkSelectionFrame() noexcept;
  Result<void> writeConfig(const Config& config) noexcept;
  Result<void> readBackConfig(const Config& config) noexcept;
  Result<void> enterStandby(const Config& config) noexcept;
  Result<void> enterMode(const Config& config, PowerMode mode) noexcept;
  Error checkModeReadBack(const Config& config, PowerMode mode) noexcept;
  Result<void> afterFailedSequence(Error own) noexcept;
  Result<void> standbyAfterStart() noexcept;
  Result<Measurement> readData(Verify verify) noexcept;
  Result<Measurement> confirmData(std::span<const uint8_t, 6> first) noexcept;
  Result<uint8_t> readFifoCountRaw() noexcept;
  Result<std::size_t> readFifoFrames(std::span<FifoSample> out, uint32_t count) noexcept;
  Result<uint8_t> readIntStatus() noexcept;
  void noteIntStatus(uint8_t raw) noexcept;
  Result<uint8_t> checkSentinel() noexcept;
  [[nodiscard]] bool sentinelEvidence(const Result<std::array<uint8_t, 2>>& pair) const noexcept;
  Result<std::array<uint8_t, 2>> readSentinelPair() noexcept;
  Error settle(Error own) noexcept;
  Error porContradicted(Error own) noexcept;
  Result<bool> configHeld() noexcept;
  Result<uint8_t> checkBeforeWrite() noexcept;
  Result<void> gate() noexcept;
  Result<uint8_t> readByte(uint8_t reg) noexcept;
  Error read(uint8_t reg, std::size_t count) noexcept;
  Error write(uint8_t reg, uint8_t value) noexcept;
  Error markReset() noexcept;
  Result<void> fail(Error error, const char* message) noexcept;

  Bus bus_;
  Config config_{};
  std::array<uint8_t, kFifoMaxBytes> buffer_{}; ///< Every read lands here (KB-HW-019).
  uint8_t revision_{0};
  uint8_t last_int_status_{0}; ///< INT_STATUS as last read (GetLastIntStatus()).
  uint8_t last_status_{0};     ///< STATUS as last read (GetLastStatus()).
  uint8_t last_osr_eff_{0};    ///< OSR_EFF as last read (GetLastOsrEff()).
  uint8_t latched_int_{0};     ///< INT_STATUS bits seen since ClearLatchedInterrupts().
  bool initialized_{false};
  bool started_once_{false};    ///< EnsureInitialized() succeeded once: standby is never refused.
  bool config_known_{false};    ///< The chip holds config_, written and read back.
  bool check_pending_{false};   ///< A reset check is due: a read that could have shown it failed.
  bool por_unconfirmed_{false}; ///< INT_STATUS showed por; the reset check has not confirmed it.
  bool marker_fresh_{false};    ///< NVM_DATA was written or verified in standby, with no
                                ///< measurement from deep standby since.
  bool sentinel_doubt_{false};  ///< A configuration sequence has written: a reset during it may
                                ///< be covered, so a failure leaves the driver uninitialised.
};

/** @} */

} // namespace bmp581

// codeql::misra_deviation_next_line(MD-CPP-009)
#include "../src/bmp581.ipp" // IWYU pragma: export

#endif // BMP581_HPP
