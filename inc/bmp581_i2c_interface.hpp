// clang-format off: the table below must keep one row per line.
/**
 * @file bmp581_i2c_interface.hpp
 * @brief The I2C port of the BMP581 driver: the CRTP base, the `I2cPort` concept, the bus
 * requirements and the register framing.
 * @copyright Copyright (c) 2026 The driver-bmp581 authors. SPDX-License-Identifier: MIT
 *
 * @details
 * The driver never calls an SDK. On I2C it calls hooks on a class you derive from
 * `bmp581::I2cInterface<Self>`:
 *
 * | Hook | Contract |
 * | --- | --- |
 * | `bool EnsureInitialized() noexcept` | Bring the bus up on the first call; `true` at once afterwards |
 * | `bool Write(uint8_t addr, const uint8_t* data, std::size_t len) noexcept` | START, `addr` + W, `len` bytes, STOP; `false` on a NACK or bus error |
 * | `bool WriteRead(uint8_t addr, const uint8_t* tx, std::size_t tx_len, uint8_t* rx, std::size_t rx_len) noexcept` | START, `addr` + W, `tx`, repeated START (or STOP and START), `addr` + R, `rx_len` bytes, STOP |
 * | `bool GetLastFailure(I2cFailure& failure) noexcept` (optional) | After `Write()` or `WriteRead()` returned `false`: why. `true` and `failure` set when the port knows |
 *
 * `addr` is the 7-bit address. The port moves raw bytes: the register address travels in the
 * payload, and the port never retries or configures the chip. What the chip needs from the bus is
 * below as `constexpr` values in `bmp581::i2c`; a port checks its settings against them with
 * `static_assert`. `DelayUs()` and `Log()` are described in `bmp581.hpp`.
 *
 * Section, table and figure numbers refer to the Bosch BMP581 datasheet, document
 * BST-BMP581-DS004-13, revision 1.13.
 */
// clang-format on
#ifndef BMP581_I2C_INTERFACE_HPP
#define BMP581_I2C_INTERFACE_HPP

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>

#include "bmp581_detail.hpp"
#include "bmp581_registers.hpp"
#include "bmp581_types.hpp"

/**
 * @brief What the BMP581 needs from the I2C bus (sections 5.6, 6.2.3, table 4). A port and a board
 * configuration check their settings against these with `static_assert` (handbook KB-CPP-123).
 */
namespace bmp581::i2c {
/// 7-bit address with SDO low (section 5.6). SDO must not float on I2C.
inline constexpr uint8_t kI2cAddress = 0x46;
/// 7-bit address with SDO high (section 5.6).
inline constexpr uint8_t kI2cAddressSdoHigh = 0x47;
/// Highest SCL frequency: fast mode plus, with C_bus below 100 pF (table 4, f_i2c).
inline constexpr uint32_t kMaxClockHz = 1'000'000;
/// Lowest SCL frequency (table 4, f_i2c).
inline constexpr uint32_t kMinClockHz = 100'000;
/// `false`: the chip keeps the register address across a STOP, so a read may use a repeated START
/// or a STOP and a START (section 5.6.2, figures 15 and 16). The driver always calls `WriteRead()`.
inline constexpr bool kRepeatedStart = false;
} // namespace bmp581::i2c

namespace bmp581 {

/** @brief Why an I2C transfer failed, as the optional `GetLastFailure()` hook reports it. */
enum class I2cFailure : uint8_t {
  Other = 0,       ///< Arbitration lost, a stuck line, a timeout, or a NACK the port cannot place.
  AddressNack = 1, ///< No device acknowledged the address byte.
  DataNack = 2,    ///< The device acknowledged its address, then not a data byte.
};

/**
 * @brief CRTP base of the platform I2C port.
 *
 * Derive `class MyPort : public bmp581::I2cInterface<MyPort>` and implement the hooks in this
 * file's description. The base declares no hook itself (a forwarding hook would turn a missing
 * override into infinite recursion) and no data; the driver checks your class with the `I2cPort`
 * concept, so a missing or misdeclared hook is a compile error that names it.
 *
 * @tparam Derived Your port class.
 */
template <typename Derived>
class I2cInterface {
public:
  /** @brief Ports are not copied: the driver holds a reference. */
  I2cInterface(const I2cInterface&) = delete;
  /** @brief Ports are not copied: the driver holds a reference. */
  I2cInterface& operator=(const I2cInterface&) = delete;

protected:
  /** @brief Only the derived port is constructed. */
  I2cInterface() noexcept = default;
  /** @brief Destroyed through the derived port only. */
  ~I2cInterface() noexcept = default;
};

/**
 * @brief Satisfied by a class that derives from `I2cInterface<Port>` and implements the required
 * hooks with exactly the types above, `noexcept` and returning `bool`.
 *
 * Each hook's address is matched with `detail::ExactHook`, which takes a member function of exactly
 * the signature and converts no value: a hook declared with a narrower length or without `noexcept`
 * does not satisfy the concept (handbook KB-CPP20-006).
 *
 * @tparam Port The port class.
 */
template <typename Port>
concept I2cPort = std::derived_from<Port, I2cInterface<Port>> && requires {
  detail::ExactHook<Port, bool() noexcept>::Take(&Port::EnsureInitialized);
  detail::ExactHook<Port, bool(uint8_t, const uint8_t*, std::size_t) noexcept>::Take(&Port::Write);
  detail::ExactHook<Port, bool(uint8_t, const uint8_t*, std::size_t, uint8_t*,
                               std::size_t) noexcept>::Take(&Port::WriteRead);
};

namespace detail {

/// The port declares something called `GetLastFailure` (used to reject a misdeclared hook).
template <typename Port>
concept DeclaresGetLastFailure = requires {
  &Port::GetLastFailure;
} || requires(Port& port, I2cFailure& failure) { port.GetLastFailure(failure); };

/// The port explains a failed transfer: `bool GetLastFailure(I2cFailure& failure) noexcept`.
template <typename Port>
concept HasGetLastFailure =
    requires { ExactHook<Port, bool(I2cFailure&) noexcept>::Take(&Port::GetLastFailure); };

/// Longest read burst the driver issues, in data bytes: a full FIFO (section 4.6).
inline constexpr std::size_t kMaxBurst = kFifoMaxBytes;

/**
 * Register access over I2C (section 5.6): the register address is the first payload byte, a read
 * is one combined write-read, and every write is one register in one transfer, which the chip
 * stores whole or not at all. Buffers are spans (KB-CPP20-042), checked before the first access
 * (KB-CPP20-043).
 */
template <I2cPort Port>
class I2cRegisterBus {
public:
  /// Bind to a port and a 7-bit address.
  explicit I2cRegisterBus(Port& port, uint8_t address = i2c::kI2cAddress) noexcept
      : port_(port), address_(address) {}

  /// The port.
  Port& GetPort() & noexcept {
    return port_;
  }

  /// The 7-bit address.
  [[nodiscard]] uint8_t GetAddress() const noexcept {
    return address_;
  }

  /// Whether the address is one the chip answers to: 0x46 or 0x47 (section 5.6).
  [[nodiscard]] bool AddressValid() const noexcept {
    return (address_ == i2c::kI2cAddress) || (address_ == i2c::kI2cAddressSdoHigh);
  }

  /// I2C needs no interface selection (section 5.1): the chip starts in I2C mode.
  Error SelectInterface() noexcept {
    return Error::Ok;
  }

  /// Read `out.size()` registers from `reg` in one transfer. `InvalidArgument` (nothing sent) for 0
  /// or more than `kMaxBurst` bytes; `NoDevice` or `Bus` on a failed transfer.
  Error Read(uint8_t reg, std::span<uint8_t> out) noexcept {
    if (out.empty() || (out.size() > kMaxBurst)) {
      return Error::InvalidArgument;
    }
    const std::array<uint8_t, 1> pointer{reg};
    if (port_.WriteRead(address_, pointer.data(), pointer.size(), out.data(), out.size())) {
      return Error::Ok;
    }
    return failure();
  }

  /// Write one register in one transfer. `NoDevice` or `Bus` on a failed transfer.
  Error WriteByte(uint8_t reg, uint8_t value) noexcept {
    const std::array<uint8_t, 2> frame{reg, value};
    return port_.Write(address_, frame.data(), frame.size()) ? Error::Ok : failure();
  }

  /// Write CMD = 0xB6. Over I2C the chip does not acknowledge the data byte (section 7.36): a
  /// failed transfer is the expected outcome, and only an address NACK (`NoDevice`, with
  /// `GetLastFailure()`) is an error. The reads that follow prove the reset (KB-CPP-130).
  Error WriteResetCommand() noexcept {
    const std::array<uint8_t, 2> frame{reg::kCmd, reg::kCmdSoftReset};
    if (port_.Write(address_, frame.data(), frame.size())) {
      return Error::Ok;
    }
    return (failure() == Error::NoDevice) ? Error::NoDevice : Error::Ok;
  }

private:
  /// `NoDevice` for an address NACK the port reports, `Bus` otherwise.
  Error failure() noexcept {
    if constexpr (HasGetLastFailure<Port>) {
      I2cFailure reason = I2cFailure::Other;
      if (port_.GetLastFailure(reason) && (reason == I2cFailure::AddressNack)) {
        return Error::NoDevice;
      }
    }
    return Error::Bus;
  }

  Port& port_;
  uint8_t address_;
};

} // namespace detail

} // namespace bmp581

#endif // BMP581_I2C_INTERFACE_HPP
