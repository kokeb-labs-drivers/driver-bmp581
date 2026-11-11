/**
 * @file fuzz_bmp581.cpp
 * @brief Fuzz target: the driver over a bus and a chip whose behaviour the fuzzer controls, with
 * the safety and validity rules as invariants.
 *
 * The fuzzer's bytes choose the bus (I2C or SPI), a configuration, then a sequence of driver
 * calls. On every transfer they also choose what the bus does: pass it to the chip model, fail it
 * before it reaches the chip, lose the acknowledgement of a completed transfer, flip bits of the
 * data read or drop an acknowledged write, replace the data read by a line stuck low or floating
 * high, or reset the chip just before it. Between calls they can reset the chip, let time pass,
 * change the pressure and temperature (inside and outside the measurement range, the reset value
 * 0x7F7F7F included) and flip the model's readings of the datasheet's open questions.
 *
 * After every call the target checks the driver's promises (docs/contract.md), and aborts, naming
 * the line, when one breaks:
 *
 * - a call returns an `Error` the enum defines; a failed value is `T{}`;
 * - before `EnsureInitialized()` first succeeds, no call but `Deinitialize()` makes a transfer;
 * - `InvalidArgument` means nothing was sent;
 * - `DeviceReset` leaves the driver uninitialised;
 * - no stuck-line value is ever data: a call whose data read was replaced by all 0x00 or all 0xFF
 *   never returns `Ok`;
 * - every measurement and FIFO sample returned `Ok` is inside the measurement range and never the
 *   reset value; when no read of the call was tampered with, it is exactly what the chip holds;
 * - a reset is never missed: after a power-on reset, no call that checks for one
 * (`CheckForReset()`, `ApplyConfig()`, `SetPowerMode()` to a measuring mode, `MeasureOnce()`,
 * `ReadMeasurement()`, `ReadFifo()`, `WaitDataReady()`) returns `Ok` until `EnsureInitialized()`
 * soft-resets the chip, except a call during which a read was flipped or replaced or the reset
 * happened (the faults that call cannot see; a dropped write is no excuse), and the next call is
 * checked again;
 * - nothing is written over a reset chip: no write but a standby or the soft reset lands on it,
 *   except in a call during which the reset happened or a read was tampered with, and in
 *   `EnsureInitialized()`, which starts from a soft reset by design;
 * - after a call in which the chip reset and a read was disturbed, the call's own writes may have
 *   restored what a reset check reads while the disturbed read hid the por: the two reset promises
 *   are then skipped until the next `Ok` `EnsureInitialized()`, which checks them again.
 *
 * The bus can also let 2 ms pass before a transfer (a pre-empted caller), the chip may answer at
 * once after a reset or stay silent for its boot time, and on SPI the first frame after a reset
 * only selects SPI again, with all 0x00, all 0xFF, 0xA5 bytes or the frame's own bytes as its data.
 * - every log line is a non-empty plain-text message.
 *
 * Built two ways (tests/fuzz/CMakeLists.txt): with libFuzzer (Clang, `-DBMP581_BUILD_FUZZERS=ON`),
 * and as a plain executable that replays the corpus in tests/fuzz/corpus/ as a CTest on any
 * compiler.
 */
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <string_view>

#include "bmp581.hpp"
#include "mock_bmp581.hpp" // the chip model, then the bus mock in mock_bmp581_bus.hpp

namespace {

using bmp581::Error;
using bmp581_test::Bmp581Model;

/// The fuzzer's bytes, read front to back; zeros once they run out.
class Input {
public:
  Input(const uint8_t* data, std::size_t size) noexcept : data_(data), size_(size) {}

  [[nodiscard]] bool Empty() const noexcept {
    return pos_ >= size_;
  }
  uint8_t Byte() noexcept {
    return pos_ < size_ ? data_[pos_++] : uint8_t{0};
  }
  uint32_t Word() noexcept {
    const auto high = Byte();
    return (static_cast<uint32_t>(high) << 8U) | Byte();
  }

private:
  const uint8_t* data_;
  std::size_t size_;
  std::size_t pos_ = 0;
};

/// A broken promise: name the line and abort, so libFuzzer saves the input as a crash.
void Check(bool promise, int line = __builtin_LINE()) noexcept {
  if (!promise) {
    (void)std::fprintf(stderr, "fuzz_bmp581: promise broken at line %d\n", line);
    std::abort();
  }
}

/// What the bus does with one transfer, chosen by the fuzzer; any other value passes it.
constexpr uint8_t kFail = 0xFF;     ///< The transfer fails; it never reached the chip.
constexpr uint8_t kTamper = 0xFE;   ///< A read: bits flipped (noise). A write: dropped after ACK.
constexpr uint8_t kStuckLow = 0xFD; ///< The data read is all 0x00.
constexpr uint8_t kFloat = 0xFC;    ///< The data read is all 0xFF.
constexpr uint8_t kReset = 0xFB;    ///< A power-on reset just before the transfer.
constexpr uint8_t kLostAck = 0xFA;  ///< The transfer completes on the chip, reported failed.
constexpr uint8_t kDelay = 0xF9;    ///< 2 ms pass before the transfer (the caller was pre-empted).

/// What happened on the bus during one driver call.
struct CallRecord {
  int transfers = 0;
  bool tampered = false;      ///< A read was flipped or replaced, or a write dropped.
  bool read_tampered = false; ///< A read was flipped or replaced: what the driver could not see.
  bool reset = false;         ///< The chip reset during the call.
  bool data_stuck = false;    ///< The data or FIFO read was replaced by a stuck or floating line.
  bool data_read = false;     ///< The chip answered a data read; `data` is what it sent.
  std::array<uint8_t, 6> data{};
};

/// The bus the fuzzer controls, in front of the chip model.
struct FuzzBus {
  Input& in;
  Bmp581Model chip;
  CallRecord call;
  int log_lines = 0;

  explicit FuzzBus(Input& input) noexcept : in(input) {}

  uint8_t Action() noexcept {
    ++call.transfers;
    const uint8_t a = in.Byte();
    if (a == kReset) {
      chip.PowerOnReset();
      call.reset = true;
    } else if (a == kDelay) {
      chip.Elapse(2000U);
    } else {
      // Any other value passes the transfer, or is handled by the read or write.
    }
    return a;
  }

  /// A read of `n` bytes from `reg` into `rx`; false when the transfer fails.
  bool Read(uint8_t reg, uint8_t* rx, std::size_t n, uint8_t action) noexcept {
    if ((action == kFail) || chip.Busy()) {
      chip.Elapse(Bmp581Model::kByteUs);
      return false;
    }
    if ((action == kStuckLow) || (action == kFloat)) {
      Bmp581Model::FillOut(rx, n, (action == kFloat) ? uint8_t{0xFF} : uint8_t{0x00});
      call.tampered = true;
      call.read_tampered = true;
      // The 6-byte second read of Verify::ReadBack is outvoted, not trusted: only the frame read
      // and the FIFO read count.
      call.data_stuck = call.data_stuck || ((reg == bmp581::reg::kTempXlsb) && (n > 6U)) ||
                        (reg == bmp581::reg::kFifoData);
      return true;
    }
    chip.ReadBurst(reg, rx, n);
    if ((reg == bmp581::reg::kTempXlsb) && (n >= 6U)) {
      call.data_read = true;
      for (std::size_t i = 0; i < call.data.size(); ++i) {
        call.data[i] = rx[i];
      }
    }
    if (action == kTamper) {
      const uint8_t mask = in.Byte();
      rx[in.Byte() % n] ^= static_cast<uint8_t>(mask | 0x01U);
      call.tampered = true;
      call.read_tampered = true;
    }
    return action != kLostAck;
  }

  /// A one-register write; false when the transfer fails. I2C NACKs the soft reset by design.
  bool Write(uint8_t reg, uint8_t value, uint8_t action, bool i2c) noexcept {
    if ((action == kFail) || chip.Busy()) {
      chip.Elapse(Bmp581Model::kByteUs);
      return false;
    }
    if ((action == kTamper) || (action == kStuckLow) || (action == kFloat)) {
      call.tampered = true;
      return true; // acknowledged, never stored
    }
    const std::size_t acked = chip.WriteBurst(reg, &value, 1U, 1U, i2c);
    return (acked == 1U) && (action != kLostAck);
  }

  void Log(bmp581::LogLevel level, const char* message) noexcept {
    Check(static_cast<uint8_t>(level) <= static_cast<uint8_t>(bmp581::LogLevel::Debug));
    Check((message != nullptr) && (message[0] != '\0'));
    ++log_lines;
  }
};

/// The I2C port over the fuzz bus.
class FuzzI2cPort : public bmp581::I2cInterface<FuzzI2cPort> {
public:
  explicit FuzzI2cPort(FuzzBus& bus) noexcept : bus_(bus) {}
  bool EnsureInitialized() noexcept {
    return bus_.in.Byte() != kFail;
  }
  bool Write(uint8_t addr, const uint8_t* data, std::size_t len) noexcept {
    const uint8_t action = bus_.Action();
    Check((addr == bmp581::i2c::kI2cAddress) && (len == 2U));
    return bus_.Write(data[0], data[1], action, true);
  }
  bool WriteRead(uint8_t addr, const uint8_t* tx, std::size_t tx_len, uint8_t* rx,
                 std::size_t rx_len) noexcept {
    const uint8_t action = bus_.Action();
    Check((addr == bmp581::i2c::kI2cAddress) && (tx_len == 1U) && (rx_len >= 1U) &&
          (rx_len <= bmp581::kFifoMaxBytes));
    return bus_.Read(tx[0], rx, rx_len, action);
  }
  void DelayUs(uint32_t us) noexcept {
    bus_.chip.Elapse(us);
  }
  void Log(bmp581::LogLevel level, const char* message) noexcept {
    bus_.Log(level, message);
  }

private:
  FuzzBus& bus_;
};

/// The SPI port over the fuzz bus. After a power-on or soft reset the chip is in I2C mode: the next
/// frame only selects SPI, its writes are lost and its read data is invalid (section 5.1); the
/// fuzzer chooses that data among all 0x00, all 0xFF, 0xA5 bytes and the frame's own bytes.
class FuzzSpiPort : public bmp581::SpiInterface<FuzzSpiPort> {
public:
  explicit FuzzSpiPort(FuzzBus& bus) noexcept : bus_(bus) {}
  bool EnsureInitialized() noexcept {
    return bus_.in.Byte() != kFail;
  }
  bool Transfer(const uint8_t* tx, uint8_t* rx, std::size_t len) noexcept {
    const uint8_t action = bus_.Action();
    Check((len >= 2U) && (len <= (bmp581::kFifoMaxBytes + 1U)));
    if ((action != kFail) && !bus_.chip.Busy() && !bus_.chip.spi_selected) {
      bus_.chip.spi_selected = true;
      constexpr std::array<uint8_t, 3> kFill{0x00, 0xFF, 0xA5};
      const uint32_t kind = bus_.in.Byte() % 4U;
      for (std::size_t i = 0; (rx != nullptr) && (i < len); ++i) {
        rx[i] = (kind < kFill.size()) ? kFill[kind] : tx[i]; // the last kind: the frame's own bytes
      }
      return true;
    }
    if ((tx[0] & 0x80U) != 0U) {
      if (rx == nullptr) {
        Check(false);
        return false;
      }
      rx[0] = 0xFF;
      return bus_.Read(static_cast<uint8_t>(tx[0] & 0x7FU), rx + 1, len - 1U, action);
    }
    Check(len == 2U);
    return bus_.Write(tx[0], tx[1], action, false);
  }
  void DelayUs(uint32_t us) noexcept {
    bus_.chip.Elapse(us);
  }
  void Log(bmp581::LogLevel level, const char* message) noexcept {
    bus_.Log(level, message);
  }

private:
  FuzzBus& bus_;
};

/// A configuration the fuzzer chooses; often invalid, often a preset.
bmp581::Config MakeConfig(Input& in) noexcept {
  bmp581::Config c{};
  switch (in.Byte() % 6U) {
  case 1:
    c = bmp581::ForcedPreset();
    break;
  case 2:
    c = bmp581::LowPowerPreset();
    break;
  case 3:
    c = bmp581::HighResolutionPreset();
    break;
  case 4:
    c = bmp581::FifoPreset();
    break;
  case 5: {
    const uint32_t a = in.Byte();
    const uint32_t b = in.Byte();
    c.mode = static_cast<bmp581::PowerMode>(a & 0x03U);
    c.pressure_oversampling = static_cast<bmp581::Oversampling>((a >> 2U) & 0x07U);
    c.temperature_oversampling = static_cast<bmp581::Oversampling>((a >> 5U) & 0x07U);
    c.odr = static_cast<bmp581::OutputDataRate>(b & 0x1FU);
    c.fifo_frame = static_cast<bmp581::FifoFrame>((b >> 5U) & 0x03U);
    c.deep_standby = (b & 0x80U) != 0U;
    c.fifo_threshold = static_cast<uint8_t>(in.Byte() & 0x3FU);
    c.fifo_decimation = static_cast<uint8_t>(in.Byte() & 0x0FU);
    c.source_data_ready = (in.Byte() & 1U) != 0U;
    c.pressure_iir = static_cast<bmp581::IirFilter>(in.Byte() & 0x07U);
    c.oor_reference_pa = in.Word() * 2U;
    c.oor_window_pa = in.Byte();
    c.source_out_of_range = (c.oor_window_pa & 1U) != 0U;
    c.data_ready_timeout_ms = 1U + (in.Byte() % 50U);
    break;
  }
  default:
    break;
  }
  return c;
}

/// Whether a raw pair is a value the driver may return.
bool Plausible(int32_t pressure, int32_t temperature) noexcept {
  return bmp581::PressureInRange(pressure) && bmp581::TemperatureInRange(temperature);
}

/// The data registers as the chip sent them in the call's data read.
bmp581::Measurement ChipData(const std::array<uint8_t, 6>& b) noexcept {
  const auto t = static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8U) |
                 (static_cast<uint32_t>(b[2]) << 16U);
  const auto p = static_cast<uint32_t>(b[3]) | (static_cast<uint32_t>(b[4]) << 8U) |
                 (static_cast<uint32_t>(b[5]) << 16U);
  return {.pressure_raw = bmp581::SignExtend24(p), .temperature_raw = bmp581::SignExtend24(t)};
}

/// The run: one driver on one bus, the calls the fuzzer picks, the invariants after each.
template <typename Port>
void Run(Input& in, FuzzBus& bus, Port& port) noexcept {
  const bmp581::Config config = MakeConfig(in);
  bmp581::BMP581<Port> sensor(port, config);
  bool started = false;
  bool attempted = false; // an EnsureInitialized() reached the chip: standby is never refused
  for (int step = 0; (step < 64) && !in.Empty(); ++step) {
    const uint8_t op = in.Byte();
    // Between calls: time, the environment, a reset, the model's open readings.
    switch (in.Byte() % 8U) {
    case 0:
      bus.chip.Elapse(static_cast<uint64_t>(in.Word()) * 50U);
      break;
    case 1:
      bus.chip.pressure_raw = bmp581::SignExtend24((in.Word() << 8U) | in.Byte());
      break;
    case 2:
      bus.chip.temperature_raw = bmp581::SignExtend24((in.Word() << 8U) | in.Byte());
      break;
    case 3:
      bus.chip.PowerOnReset();
      bus.chip.Elapse(static_cast<uint64_t>(in.Byte()) * 50U);
      break;
    case 4:
      bus.chip.standby_delay_us = (in.Byte() & 1U) != 0U ? 0U : 1000U;
      bus.chip.flags_for_disabled_sources = (in.Byte() & 1U) != 0U;
      break;
    case 5:
      bus.chip.deep_standby_drops_writes = (in.Byte() & 1U) != 0U;
      bus.chip.odr_valid_lag_us = static_cast<uint32_t>(in.Byte() & 7U) * 500U;
      bus.chip.mode_lag_us = static_cast<uint32_t>(in.Byte() & 3U) * 400U;
      bus.chip.select_fill = in.Byte();
      bus.chip.nvm_download_rewrites_data = (in.Byte() & 1U) != 0U;
      bus.chip.nvm_download_data = {in.Byte(), in.Byte()}; // 0x0000 and the marker included
      break;
    default:
      break;
    }
    const bool reset_before_call = bus.chip.por_since_soft;
    const int writes_over_reset = bus.chip.writes_over_reset;
    bus.call = CallRecord{};
    Error e = Error::Ok;
    bool checks_reset = false;
    bool standby_call = false;
    bool data_ok = false;
    bmp581::Measurement m{};
    std::array<bmp581::FifoSample, 32> samples{};
    std::size_t sample_count = 0;
    switch (op % 13U) {
    case 0:
      e = sensor.EnsureInitialized().error;
      break;
    case 1: {
      const auto r = sensor.ReadMeasurement((in.Byte() & 1U) != 0U ? bmp581::Verify::ReadBack
                                                                   : bmp581::Verify::None);
      e = r.error;
      m = r.value;
      data_ok = r.ok();
      checks_reset = true;
      break;
    }
    case 2: {
      const auto r = sensor.MeasureOnce((in.Byte() & 1U) != 0U ? bmp581::Verify::ReadBack
                                                               : bmp581::Verify::None);
      e = r.error;
      m = r.value;
      data_ok = r.ok();
      checks_reset = true;
      break;
    }
    case 3:
      e = sensor.WaitDataReady().error;
      checks_reset = true;
      break;
    case 4: {
      const std::size_t room = 1U + (in.Byte() % samples.size());
      const auto r = sensor.ReadFifo(std::span<bmp581::FifoSample>{samples.data(), room});
      e = r.error;
      sample_count = r.value;
      Check(r.ok() || (r.value == 0U));
      checks_reset = true;
      break;
    }
    case 5:
      e = sensor.CheckForReset().error;
      checks_reset = true;
      break;
    case 6:
      e = sensor.ApplyConfig(MakeConfig(in)).error;
      checks_reset = started && sensor.IsInitialized();
      break;
    case 7: {
      const auto mode = static_cast<bmp581::PowerMode>(in.Byte() & 0x03U);
      e = sensor.SetPowerMode(mode).error;
      checks_reset = (mode == bmp581::PowerMode::Normal) || (mode == bmp581::PowerMode::Continuous);
      standby_call = mode == bmp581::PowerMode::Standby;
      break;
    }
    case 8:
      e = sensor.Deinitialize().error;
      break;
    case 9:
      e = sensor.ReadInterruptStatus().error;
      break;
    case 10:
      e = sensor.ReadFifoCount().error;
      checks_reset = sensor.GetConfig().fifo_frame != bmp581::FifoFrame::Disabled;
      break;
    case 11:
      e = sensor.ReadRegister(in.Byte()).error;
      break;
    default:
      sensor.ClearLatchedInterrupts();
      break;
    }
    const CallRecord& call = bus.call;
    // An error the enum defines.
    Check(std::string_view{bmp581::ToString(e)} != "Unknown");
    // Nothing before the first start, but Deinitialize(), and standby once a start has written.
    if (!started && ((op % 13U) != 0U) && ((op % 13U) != 8U) && !(attempted && standby_call)) {
      Check(call.transfers == 0);
    }
    attempted = attempted || (((op % 13U) == 0U) && (call.transfers > 0));
    if (e == Error::InvalidArgument) {
      Check(call.transfers == 0);
    }
    if (e == Error::DeviceReset) {
      Check(!sensor.IsInitialized());
    }
    // Values: never a stuck line, never out of range or the reset value; exact when untampered.
    if (data_ok) {
      Check(!call.data_stuck);
      Check(Plausible(m.pressure_raw, m.temperature_raw));
      Check(call.data_read || call.tampered);
      if (!call.tampered) {
        const bmp581::Measurement held = ChipData(call.data);
        Check((m.pressure_raw == held.pressure_raw) && (m.temperature_raw == held.temperature_raw));
      }
    } else {
      Check((m.pressure_raw == 0) && (m.temperature_raw == 0));
    }
    if (e == Error::Ok) {
      Check(!call.data_stuck || ((op % 13U) != 4U) || (sample_count == 0U));
      for (std::size_t i = 0; i < sample_count; ++i) {
        const bmp581::FifoSample& s = samples[i];
        if (s.error == Error::Ok) {
          Check(!s.has_pressure || bmp581::PressureInRange(s.pressure_raw));
          Check(!s.has_temperature || bmp581::TemperatureInRange(s.temperature_raw));
        } else {
          Check(!s.has_pressure && !s.has_temperature && (s.pressure_raw == 0));
        }
      }
    }
    // A reset is never missed by a call that checks for one, unless this call could not see it.
    if (checks_reset && (e == Error::Ok) && reset_before_call && !call.read_tampered &&
        !call.reset) {
      Check(false);
    }
    // Nothing is written over a reset chip that the driver could have seen; EnsureInitialized()
    // starts from a soft reset and may write a reset chip by design.
    if (!call.reset && !call.read_tampered && reset_before_call && ((op % 13U) != 0U)) {
      Check(bus.chip.writes_over_reset == writes_over_reset);
    }
    if ((e == Error::Ok) && ((op % 13U) == 0U)) {
      // An Ok start configured a chip that came out of a reset, whichever reset it was: a power-on
      // reset that hit the soft reset write itself leaves the same state as the soft reset.
      started = true;
      bus.chip.por_since_soft = false;
    }
  }
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size) {
  Input in(data, size);
  FuzzBus bus(in);
  const uint32_t setup = in.Byte();
  bus.chip.reset_busy_us =
      ((setup & 2U) != 0U) ? 0U : 1500U; // answers at once after a reset, or not
  if ((setup & 1U) == 0U) {
    FuzzI2cPort port(bus);
    Run(in, bus, port);
  } else {
    FuzzSpiPort port(bus);
    Run(in, bus, port);
  }
  return 0;
}
