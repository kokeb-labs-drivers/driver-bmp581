/**
 * @file ports.cpp
 * @brief The ports with every optional hook, as shown on the Porting page. Compiled, never run.
 */
#include <cstddef>
#include <cstdint>

#include "bmp581.hpp"
#include "my_port.hpp"

enum class PlatformI2cError : uint8_t { None, AddressNack, DataNack, Other };
PlatformI2cError platform_i2c_last_error() noexcept;
void platform_log(const char* line) noexcept;

// --8<-- [start:full-port]
class FullPort : public bmp581::I2cInterface<FullPort> {
public:
  static constexpr uint8_t kAddress = bmp581::i2c::kI2cAddress; // SDO tied low
  static constexpr uint32_t kClockHz = 400'000;
  static_assert(kClockHz <= bmp581::i2c::kMaxClockHz, "SCL above the BMP581's maximum");

  bool EnsureInitialized() noexcept {
    return platform_i2c_init();
  }
  bool Write(uint8_t addr, const uint8_t* data, std::size_t len) noexcept {
    return platform_i2c_write(addr, data, len);
  }
  bool WriteRead(uint8_t addr, const uint8_t* tx, std::size_t tx_len, uint8_t* rx,
                 std::size_t rx_len) noexcept {
    return platform_i2c_write_read(addr, tx, tx_len, rx, rx_len);
  }

  // Required by every call that waits (power-up, soft reset, standby, polls).
  void DelayUs(uint32_t us) noexcept {
    platform_delay_us(us);
  }

  // Optional: why the last transfer failed, so an absent chip is NoDevice rather than Bus.
  bool GetLastFailure(bmp581::I2cFailure& failure) noexcept {
    switch (platform_i2c_last_error()) {
    case PlatformI2cError::AddressNack:
      failure = bmp581::I2cFailure::AddressNack;
      return true;
    case PlatformI2cError::DataNack:
      failure = bmp581::I2cFailure::DataNack;
      return true;
    case PlatformI2cError::None:
    case PlatformI2cError::Other:
    default:
      break;
    }
    return false;
  }

  // Optional: one line per lifecycle event or failure. The driver never formats; the port does.
  void Log(bmp581::LogLevel level, const char* message) noexcept {
    if (level > bmp581::LogLevel::Warning) {
      return;
    }
    platform_log(message); // plain text: never use it as a format string
  }
};
// --8<-- [end:full-port]

// --8<-- [start:spi-driver]
bmp581::Error StartOnSpi(MySpiPort& port) {
  // The same driver class on SPI: it sends the dummy read that selects SPI itself.
  static bmp581::BMP581<MySpiPort> sensor(port, bmp581::LowPowerPreset());
  return sensor.EnsureInitialized().error;
}
// --8<-- [end:spi-driver]

bmp581::Error StartFull(FullPort& port) {
  static bmp581::BMP581<FullPort> sensor(port, FullPort::kAddress);
  return sensor.EnsureInitialized().error;
}
