/**
 * @file host_bmp581_port.hpp
 * @brief The host port: the driver's bus hooks over a simulated BMP581, so the example runs on any
 * PC.
 *
 * `bmp581_test::MockPortBase` (tests/mock_bmp581_bus.hpp) supplies the required bus hooks over
 * the register model the host tests use. This class adds what a real port adds: `DelayUs()` (here
 * it advances the simulated chip's clock instead of waiting) and `Log()` to the console. A port for
 * a real bus on a PC (Linux i2c-dev, a USB bridge) implements the same hooks: see docs/porting.md.
 */
#ifndef BMP581_HOST_BMP581_PORT_HPP
#define BMP581_HOST_BMP581_PORT_HPP

#include <cstdint>
#include <cstdio>

#include "mock_bmp581.hpp"

/// The host port: simulated chip, real console.
class HostBmp581Port : public bmp581_test::MockPortBase<HostBmp581Port> {
public:
  /// The bus settings a real port would use; checked against the driver's bus requirements
  /// (handbook KB-CPP-123) like any port's.
  static constexpr uint32_t kClockHz = 100'000;
  static_assert((kClockHz >= bmp581::i2c::kMinClockHz) && (kClockHz <= bmp581::i2c::kMaxClockHz),
                "SCL outside the BMP581's range");

  /// Time passes in the simulated chip; nothing sleeps.
  void DelayUs(uint32_t us) noexcept {
    state.chip.Elapse(us);
  }

  /// Driver messages (they carry their own `bmp581:` prefix) go to the console, one line each.
  void Log(bmp581::LogLevel level, const char* message) noexcept {
    (void)level;
    (void)std::puts(message); // plain text: never pass it as a format string
  }
};

#endif // BMP581_HOST_BMP581_PORT_HPP
