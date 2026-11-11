/**
 * @file mock_bmp581_bus.hpp
 * @brief I2C and SPI mock ports over the register model, with fault injection. The transaction
 * decoding here is the only bus-specific part of the host tests.
 *
 * | Port | Hooks |
 * | --- | --- |
 * | `MinimalPort` | I2C: the required ones plus `DelayUs()` |
 * | `FullPort` | I2C: also `GetLastFailure()` and `Log()` |
 * | `MinimalSpiPort` | SPI: the required ones plus `DelayUs()` |
 * | `FullSpiPort` | SPI: also `Log()` |
 */
#ifndef BMP581_MOCK_BMP581_BUS_HPP
#define BMP581_MOCK_BMP581_BUS_HPP

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "bmp581_i2c_interface.hpp"
#include "bmp581_spi_interface.hpp"
#include "bmp581_types.hpp"
#include "mock_bmp581.hpp" // the model; it includes this file at its end, the guards make either order work

namespace bmp581_test {

/// One transfer as the master sent it.
struct Frame {
  bool write = true;          ///< A write (I2C Write(), or an SPI frame without the read flag).
  uint8_t reg = 0;            ///< The register address.
  std::vector<uint8_t> bytes; ///< The data written.
  std::size_t rx_len = 0;     ///< Bytes read.
};

/// What a read returns when a fault replaces the chip's answer (handbook KB-CPP-116).
enum class LineFault : uint8_t {
  None,      ///< The chip answers.
  StuckLow,  ///< SDA or MISO held low: every byte 0x00.
  FloatHigh, ///< The data line floating high: every byte 0xFF.
  Echo,      ///< SPI MISO looped back to MOSI (on I2C: as StuckLow).
  Deaf,      ///< Nothing answers: every transfer fails.
};

/// Shared state of the mock ports: the chip, fault injection and the transfer log.
struct MockBusState {
  Bmp581Model chip;
  bool bus_up_ok = true;        ///< EnsureInitialized() result.
  int fail_transfer = -1;       ///< Address NACK (SPI: a failed frame) on this transfer; -1 never.
  int fail_from = -1;           ///< The same on every transfer from this index; -1 never.
  int nack_transfer = -1;       ///< I2C data NACK on this transfer...
  std::size_t nack_after = 0;   ///< ...after this many data bytes (stored before the NACK).
  int lost_ack_transfer = -1;   ///< The transfer completes on the chip but is reported failed.
  int drop_transfer = -1;       ///< A write acknowledged but never stored (noise on the bus).
  int reset_before = -1;        ///< Power-on reset just before this transfer.
  int corrupt_read = -1;        ///< A read whose byte `corrupt_byte`...
  std::size_t corrupt_byte = 0; ///< ...is XORed with `corrupt_mask` (noise).
  uint8_t corrupt_mask = 0x01;
  int corrupt_reads = 1; ///< Reads corrupted from `corrupt_read` on, the mask shifted left by
                         ///< one more bit on each, so no two agree.
  LineFault line = LineFault::None; ///< What every transfer sees...
  int line_from = 0;                ///< ...from this transfer index on.
  bmp581::I2cFailure last_failure = bmp581::I2cFailure::Other;
  int transfers = 0;
  std::vector<Frame> log;

  [[nodiscard]] bool Fails(int index) const noexcept {
    return (index == fail_transfer) || ((fail_from >= 0) && (index >= fail_from)) ||
           ((line == LineFault::Deaf) && (index >= line_from));
  }

  [[nodiscard]] bool LineFaulty(int index) const noexcept {
    return (line != LineFault::None) && (line != LineFault::Deaf) && (index >= line_from);
  }

  /// Start a transfer: the index, a reset when one is armed.
  int Begin() noexcept {
    const int index = transfers++;
    if (index == reset_before) {
      chip.PowerOnReset();
    }
    return index;
  }

  /// Replace read bytes by what a faulty line gives.
  void FaultRead(int index, const uint8_t* tx, uint8_t* rx, std::size_t len) const noexcept {
    if (rx == nullptr) {
      return;
    }
    for (std::size_t i = 0; i < len; ++i) {
      if (line == LineFault::FloatHigh) {
        rx[i] = 0xFF;
      } else if ((line == LineFault::Echo) && (tx != nullptr)) {
        rx[i] = tx[i];
      } else {
        rx[i] = 0x00;
      }
    }
    (void)index;
  }

  void Corrupt(int index, uint8_t* data, std::size_t len) const noexcept {
    const int k = index - corrupt_read;
    if ((corrupt_read >= 0) && (k >= 0) && (k < corrupt_reads) && (corrupt_byte < len)) {
      const auto shift = static_cast<uint32_t>(k);
      const auto mask =
          static_cast<uint8_t>((static_cast<uint32_t>(corrupt_mask) << shift) & 0xFFU);
      data[corrupt_byte] = static_cast<uint8_t>(data[corrupt_byte] ^ mask);
    }
  }

  bool I2cWrite(uint8_t addr, const uint8_t* data, std::size_t len) noexcept {
    const int index = Begin();
    log.push_back(Frame{true, (len > 0U) ? data[0] : uint8_t{0},
                        std::vector<uint8_t>(data + ((len > 0U) ? 1 : 0), data + len), 0});
    if (Fails(index) || chip.Busy() || chip.spi_selected || (len == 0U) ||
        ((addr != bmp581::i2c::kI2cAddress) && (addr != bmp581::i2c::kI2cAddressSdoHigh))) {
      last_failure = bmp581::I2cFailure::AddressNack;
      chip.Elapse(Bmp581Model::kByteUs);
      return false;
    }
    if (LineFaulty(index) || (index == drop_transfer)) {
      chip.Elapse(Bmp581Model::kByteUs * (len + 1U));
      return true; // acknowledged on a broken line, or dropped: nothing stored
    }
    const std::size_t limit = (index == nack_transfer) ? nack_after : (len - 1U);
    const std::size_t acked = chip.WriteBurst(data[0], data + 1, len - 1U, limit, true);
    if (acked < (len - 1U)) {
      last_failure = bmp581::I2cFailure::DataNack;
      return false;
    }
    if (index == lost_ack_transfer) {
      last_failure = bmp581::I2cFailure::Other;
      return false;
    }
    return true;
  }

  bool I2cWriteRead(uint8_t addr, const uint8_t* tx, std::size_t tx_len, uint8_t* rx,
                    std::size_t rx_len) noexcept {
    const int index = Begin();
    log.push_back(Frame{false, (tx_len > 0U) ? tx[0] : uint8_t{0}, {}, rx_len});
    if (Fails(index) || chip.Busy() || chip.spi_selected || (tx_len != 1U) ||
        ((addr != bmp581::i2c::kI2cAddress) && (addr != bmp581::i2c::kI2cAddressSdoHigh))) {
      last_failure = bmp581::I2cFailure::AddressNack;
      chip.Elapse(Bmp581Model::kByteUs);
      return false;
    }
    if (LineFaulty(index)) {
      FaultRead(index, nullptr, rx, rx_len);
      chip.Elapse(Bmp581Model::kByteUs * (rx_len + 2U));
      return true; // the line, not the chip, made the bytes
    }
    chip.ReadBurst(tx[0], rx, rx_len);
    Corrupt(index, rx, rx_len);
    if (index == lost_ack_transfer) {
      last_failure = bmp581::I2cFailure::Other;
      return false;
    }
    return true;
  }

  bool SpiTransfer(const uint8_t* tx, uint8_t* rx, std::size_t len) noexcept {
    const int index = Begin();
    const bool is_read = (len > 0U) && ((tx[0] & 0x80U) != 0U);
    log.push_back(Frame{!is_read, (len > 0U) ? static_cast<uint8_t>(tx[0] & 0x7FU) : uint8_t{0},
                        is_read ? std::vector<uint8_t>{}
                                : std::vector<uint8_t>(tx + ((len > 0U) ? 1 : 0), tx + len),
                        is_read ? len - 1U : 0U});
    if (Fails(index) || (len == 0U)) {
      chip.Elapse(Bmp581Model::kByteUs);
      return false;
    }
    if (chip.Busy()) {
      Bmp581Model::FillOut(rx, len, 0x00);
      chip.Elapse(Bmp581Model::kByteUs * (len + 1U));
      return true; // no ACK on SPI: a frame to a chip in reset just reads nothing
    }
    if (LineFaulty(index) && is_read) {
      FaultRead(index, tx, rx, len);
      chip.Elapse(Bmp581Model::kByteUs * (len + 1U));
      return true;
    }
    if ((LineFaulty(index) || (index == drop_transfer)) && !is_read) {
      chip.Elapse(Bmp581Model::kByteUs * (len + 1U));
      return true;
    }
    chip.SpiFrame(tx, rx, len);
    if (is_read && (rx != nullptr)) {
      Corrupt(index, rx + 1, len - 1U);
    }
    return index != lost_ack_transfer;
  }

  /// The write frames to register `reg`, in order.
  [[nodiscard]] int WritesTo(uint8_t reg) const noexcept {
    int n = 0;
    for (const Frame& f : log) {
      n += (f.write && (f.reg == reg)) ? 1 : 0;
    }
    return n;
  }

  /// The read frames from register `reg`.
  [[nodiscard]] int ReadsFrom(uint8_t reg) const noexcept {
    int n = 0;
    for (const Frame& f : log) {
      n += (!f.write && (f.reg == reg)) ? 1 : 0;
    }
    return n;
  }

  /// The largest data size of any write.
  [[nodiscard]] std::size_t LongestWrite() const noexcept {
    std::size_t n = 0;
    for (const Frame& f : log) {
      if (f.write && (f.bytes.size() > n)) {
        n = f.bytes.size();
      }
    }
    return n;
  }
};

/// The required I2C hooks over a MockBusState. Derive `class P : public MockPortBase<P>`.
template <typename Self>
class MockPortBase : public bmp581::I2cInterface<Self> {
public:
  MockBusState state;
  int bus_up_calls = 0;

  bool EnsureInitialized() noexcept {
    bus_up_calls++;
    return state.bus_up_ok;
  }
  bool Write(uint8_t addr, const uint8_t* data, std::size_t len) noexcept {
    return state.I2cWrite(addr, data, len);
  }
  bool WriteRead(uint8_t addr, const uint8_t* tx, std::size_t tx_len, uint8_t* rx,
                 std::size_t rx_len) noexcept {
    return state.I2cWriteRead(addr, tx, tx_len, rx, rx_len);
  }
};

/// The required SPI hooks over a MockBusState. Derive `class P : public SpiPortBase<P>`.
template <typename Self>
class SpiPortBase : public bmp581::SpiInterface<Self> {
public:
  MockBusState state;
  int bus_up_calls = 0;

  bool EnsureInitialized() noexcept {
    bus_up_calls++;
    return state.bus_up_ok;
  }
  bool Transfer(const uint8_t* tx, uint8_t* rx, std::size_t len) noexcept {
    return state.SpiTransfer(tx, rx, len);
  }
};

/// I2C, required hooks plus the delay; time passes in the model as the driver waits.
class MinimalPort : public MockPortBase<MinimalPort> {
public:
  uint64_t delayed_us = 0; ///< Sum of DelayUs() arguments.

  void DelayUs(uint32_t us) noexcept {
    delayed_us += us;
    state.chip.Elapse(us);
  }
};

/// I2C with every optional hook, with counters.
class FullPort : public MockPortBase<FullPort> {
public:
  uint64_t delayed_us = 0;   ///< Sum of DelayUs() arguments.
  int log_lines = 0;         ///< Log() calls.
  int error_lines = 0;       ///< Log() calls at LogLevel::Error.
  bool failure_known = true; ///< GetLastFailure() result.

  void DelayUs(uint32_t us) noexcept {
    delayed_us += us;
    state.chip.Elapse(us);
  }
  bool GetLastFailure(bmp581::I2cFailure& failure) noexcept {
    failure = state.last_failure;
    return failure_known;
  }
  void Log(bmp581::LogLevel level, const char* message) noexcept {
    (void)message;
    log_lines++;
    if (level == bmp581::LogLevel::Error) {
      error_lines++;
    }
  }
};

/// SPI, required hooks plus the delay.
class MinimalSpiPort : public SpiPortBase<MinimalSpiPort> {
public:
  uint64_t delayed_us = 0; ///< Sum of DelayUs() arguments.

  void DelayUs(uint32_t us) noexcept {
    delayed_us += us;
    state.chip.Elapse(us);
  }
};

/// SPI with the optional Log() hook.
class FullSpiPort : public SpiPortBase<FullSpiPort> {
public:
  uint64_t delayed_us = 0; ///< Sum of DelayUs() arguments.
  int log_lines = 0;       ///< Log() calls.
  int error_lines = 0;     ///< Log() calls at LogLevel::Error.

  void DelayUs(uint32_t us) noexcept {
    delayed_us += us;
    state.chip.Elapse(us);
  }
  void Log(bmp581::LogLevel level, const char* message) noexcept {
    (void)message;
    log_lines++;
    if (level == bmp581::LogLevel::Error) {
      error_lines++;
    }
  }
};

} // namespace bmp581_test

#endif // BMP581_MOCK_BMP581_BUS_HPP
