/**
 * @file fixture.hpp
 * @brief A port and a driver over the register model, and helpers shared by the host tests.
 */
#ifndef BMP581_TEST_FIXTURE_HPP
#define BMP581_TEST_FIXTURE_HPP

#include <cstdint>

#include "bmp581.hpp"
#include "mock_bmp581.hpp"

namespace bmp581_test {

/// A value of `E` that is none of its enumerators, to test the checks against one.
template <typename E>
E NotAnEnumerator(uint8_t raw) noexcept {
  return static_cast<E>(raw); // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange): on purpose
}

/// A port and a driver bound to it.
template <typename P>
struct Rig {
  P port;
  bmp581::BMP581<P> sensor;

  explicit Rig(const bmp581::Config& config = {}) noexcept : port(), sensor(port, config) {}

  Bmp581Model& Chip() noexcept {
    return port.state.chip;
  }
  MockBusState& Bus() noexcept {
    return port.state;
  }
  /// Start the driver; true on success.
  bool Start() noexcept {
    return sensor.EnsureInitialized().ok();
  }
  /// The index the next transfer will have.
  [[nodiscard]] int Next() const noexcept {
    return port.state.transfers;
  }
};

/// A raw pressure for a value in pascals (exact for multiples of 1/64 Pa).
constexpr int32_t RawPa(int32_t pa) noexcept {
  return pa * 64;
}
/// A raw temperature for a whole number of degrees Celsius.
constexpr int32_t RawC(int32_t c) noexcept {
  return c * 65536;
}

} // namespace bmp581_test

#endif // BMP581_TEST_FIXTURE_HPP
