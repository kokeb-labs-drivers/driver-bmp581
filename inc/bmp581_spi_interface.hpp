// clang-format off: the table below must keep one row per line.
/**
 * @file bmp581_spi_interface.hpp
 * @brief The SPI port of the BMP581 driver: the CRTP base, the `SpiPort` concept, the bus
 * requirements and the register framing.
 * @copyright Copyright (c) 2026 The driver-bmp581 authors. SPDX-License-Identifier: MIT
 *
 * @details
 * The driver never calls an SDK. On SPI it calls hooks on a class you derive from
 * `bmp581::SpiInterface<Self>`:
 *
 * | Hook | Contract |
 * | --- | --- |
 * | `bool EnsureInitialized() noexcept` | Bring the bus up on the first call, CSB high before the first frame; `true` at once afterwards |
 * | `bool Transfer(const uint8_t* tx, uint8_t* rx, std::size_t len) noexcept` | One full-duplex frame, CSB low for all `len` bytes; `rx` may be `nullptr` |
 *
 * Four-wire SPI only (section 6.2.2): the driver never sets DRIVE_CONFIG.spi3_en. The chip starts
 * in I2C mode after a power-on or soft reset and changes to SPI on the first frame with at least
 * 16 SCK edges, whose data is invalid (section 5.1); the driver sends that dummy read itself, in
 * `EnsureInitialized()` and after each soft reset. The port moves raw bytes: it never retries and
 * never interprets the frame. What the chip needs from the bus is below as `constexpr` values in
 * `bmp581::spi`; a port checks its settings against them with `static_assert`.
 *
 * Section, table and figure numbers refer to the Bosch BMP581 datasheet, document
 * BST-BMP581-DS004-13, revision 1.13.
 */
// clang-format on
#ifndef BMP581_SPI_INTERFACE_HPP
#define BMP581_SPI_INTERFACE_HPP

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>

#include "bmp581_detail.hpp"
#include "bmp581_registers.hpp"
#include "bmp581_types.hpp"

/**
 * @brief What the BMP581 needs from the SPI bus (sections 5.2.1, 5.5, table 18). A port and a board
 * configuration check their settings against these with `static_assert` (handbook KB-CPP-123).
 */
namespace bmp581::spi {
/// SPI mode 0 (CPOL 0, CPHA 0); the chip also accepts mode 3, chosen by SCK at the CSB falling edge
/// (section 5.5).
inline constexpr uint8_t kSpiMode = 0;
/// Each byte is clocked most significant bit first (figures 8 to 11).
inline constexpr bool kMsbFirst = true;
/// Highest SCK frequency with VDDIO at or above 1.62 V and C_bus up to 80 pF (table 4, f_spi).
inline constexpr uint32_t kMaxClockHz = 12'000'000;
/// Highest SCK frequency below VDDIO 1.62 V with C_bus up to 40 pF (table 4, f_spi_lowv).
inline constexpr uint32_t kMaxClockLowVoltageHz = 7'000'000;
/// Shortest CSB high time between two frames: the idle time after a write or a read access
/// (table 18, T_wr_idle and T_rd_idle).
inline constexpr uint32_t kMinCsHighNs = 80;
/// CSB low to the first SCK edge. Table 18 gives only the CSB lag time; the same value is assumed
/// here (**not verified on hardware**).
inline constexpr uint32_t kCsSetupNs = 40;
/// Last SCK edge to CSB high, the CSB lag time (table 18, T_hold_csb).
inline constexpr uint32_t kCsHoldNs = 40;
/// `false`: SCK may pause between the bytes of one frame while CSB stays low (section 5.5).
inline constexpr bool kContinuousWord = false;
} // namespace bmp581::spi

namespace bmp581 {

/**
 * @brief CRTP base of the platform SPI port.
 *
 * Derive `class MyPort : public bmp581::SpiInterface<MyPort>` and implement the hooks in this
 * file's description. The base declares no hook itself and no data; the driver checks your class
 * with the `SpiPort` concept, so a missing or misdeclared hook is a compile error that names it.
 *
 * @tparam Derived Your port class.
 */
template <typename Derived>
class SpiInterface {
public:
  /** @brief Ports are not copied: the driver holds a reference. */
  SpiInterface(const SpiInterface&) = delete;
  /** @brief Ports are not copied: the driver holds a reference. */
  SpiInterface& operator=(const SpiInterface&) = delete;

protected:
  /** @brief Only the derived port is constructed. */
  SpiInterface() noexcept = default;
  /** @brief Destroyed through the derived port only. */
  ~SpiInterface() noexcept = default;
};

/**
 * @brief Satisfied by a class that derives from `SpiInterface<Port>` and implements the required
 * hooks with exactly the types above, `noexcept` and returning `bool` (KB-CPP20-006).
 *
 * @tparam Port The port class.
 */
template <typename Port>
concept SpiPort = std::derived_from<Port, SpiInterface<Port>> && requires {
  detail::ExactHook<Port, bool() noexcept>::Take(&Port::EnsureInitialized);
  detail::ExactHook<Port, bool(const uint8_t*, uint8_t*, std::size_t) noexcept>::Take(
      &Port::Transfer);
};

namespace detail {

/// First-byte flag of a read frame: bit 7 = 1 read, 0 write; bits 6..0 the address (5.5.2, 5.5.3).
inline constexpr uint32_t kSpiReadFlag = 0x80U;
/// Address bits of the first byte.
inline constexpr uint32_t kSpiAddressMask = 0x7FU;
/// Longest read burst the driver issues over SPI, in data bytes: a full FIFO (section 4.6).
inline constexpr std::size_t kSpiMaxBurst = kFifoMaxBytes;

/**
 * Register access over SPI (section 5.5): one frame per access, the read flag in bit 7 of the
 * first byte, the data from the second byte on (no dummy byte), and every write one register in one
 * frame, stored whole or not at all. The frame buffers belong to this object, so they live
 * wherever the driver does, never on a caller's stack (KB-HW-019).
 */
template <SpiPort Port>
class SpiRegisterBus {
public:
  /// Bind to a port.
  explicit SpiRegisterBus(Port& port) noexcept : port_(port) {}

  /// The port.
  Port& GetPort() & noexcept {
    return port_;
  }

  /// SPI has no address.
  [[nodiscard]] bool AddressValid() const noexcept {
    return true;
  }

  /// The dummy read that changes the chip from I2C to SPI mode: CHIP_ID, 16 SCK edges, the data
  /// discarded (section 5.1, table 16). `Bus` when the frame failed.
  Error SelectInterface() noexcept {
    return Read(reg::kChipId, std::span<uint8_t>{rx_.data(), 1U});
  }

  /// Read `out.size()` registers from `reg` in one frame. `InvalidArgument` (nothing sent) for 0 or
  /// more than `kSpiMaxBurst` bytes; `Bus` on a failed frame.
  Error Read(uint8_t reg, std::span<uint8_t> out) noexcept {
    if (out.empty() || (out.size() > kSpiMaxBurst)) {
      return Error::InvalidArgument;
    }
    const std::size_t count = out.size();
    tx_.fill(0U);
    tx_[0] = static_cast<uint8_t>(kSpiReadFlag | (static_cast<uint32_t>(reg) & kSpiAddressMask));
    if (!port_.Transfer(tx_.data(), rx_.data(), count + 1U)) {
      return Error::Bus;
    }
    for (std::size_t i = 0U; i < count; ++i) {
      out[i] = rx_[1U + i];
    }
    return Error::Ok;
  }

  /// Write one register in one frame (figure 8). `Bus` on a failed frame.
  Error WriteByte(uint8_t reg, uint8_t value) noexcept {
    const std::array<uint8_t, 2> frame{
        static_cast<uint8_t>(static_cast<uint32_t>(reg) & kSpiAddressMask), value};
    return port_.Transfer(frame.data(), nullptr, frame.size()) ? Error::Ok : Error::Bus;
  }

  /// Write CMD = 0xB6; over SPI the frame completes like any write (section 7.36).
  Error WriteResetCommand() noexcept {
    return WriteByte(reg::kCmd, reg::kCmdSoftReset);
  }

private:
  Port& port_;
  std::array<uint8_t, 1U + kSpiMaxBurst> tx_{};
  std::array<uint8_t, 1U + kSpiMaxBurst> rx_{};
};

} // namespace detail

} // namespace bmp581

#endif // BMP581_SPI_INTERFACE_HPP
