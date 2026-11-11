/**
 * @file my_port.hpp
 * @brief The I2C and SPI ports shown on the Quickstart and Porting pages, compiled by the host
 * build.
 *
 * Every platform call goes through the `platform_*` functions declared below; a real port replaces
 * them with its SDK (ESP-IDF, STM32 HAL, Zephyr, Linux i2c-dev or spidev).
 */
#ifndef BMP581_MY_PORT_HPP
#define BMP581_MY_PORT_HPP

#include <cstddef>
#include <cstdint>

#include "bmp581_i2c_interface.hpp"
#include "bmp581_spi_interface.hpp"

// Your SDK's calls. Declared only: the docs build checks that the ports compile.
bool platform_i2c_init() noexcept; // 100 kHz to 1 MHz
bool platform_i2c_write(uint8_t addr7, const uint8_t* data,
                        std::size_t len) noexcept; // START .. STOP
bool platform_i2c_write_read(uint8_t addr7, const uint8_t* tx, std::size_t tx_len, uint8_t* rx,
                             std::size_t rx_len) noexcept; // repeated START
bool platform_spi_init() noexcept;                         // mode 0, up to 12 MHz, CSB high
bool platform_spi_transfer(const uint8_t* tx, uint8_t* rx,
                           std::size_t len) noexcept; // CSB low for all of it
void platform_delay_us(uint32_t us) noexcept;         // busy-wait or sleep

// --8<-- [start:port]
class MyPort : public bmp581::I2cInterface<MyPort> {
public:
  // Bring the bus up once; later calls return true at once.
  bool EnsureInitialized() noexcept {
    if (!ready_) {
      ready_ = platform_i2c_init();
    }
    return ready_;
  }

  // One write transaction: START, address + W, the bytes (register first), STOP.
  bool Write(uint8_t addr, const uint8_t* data, std::size_t len) noexcept {
    return platform_i2c_write(addr, data, len);
  }

  // Write the register address, then read with a repeated START in between.
  bool WriteRead(uint8_t addr, const uint8_t* tx, std::size_t tx_len, uint8_t* rx,
                 std::size_t rx_len) noexcept {
    return platform_i2c_write_read(addr, tx, tx_len, rx, rx_len);
  }

  // Wait at least `us` microseconds: the chip's reset and conversion times depend on it.
  void DelayUs(uint32_t us) noexcept {
    platform_delay_us(us);
  }

private:
  bool ready_ = false;
};
// --8<-- [end:port]

// --8<-- [start:spi-port]
class MySpiPort : public bmp581::SpiInterface<MySpiPort> {
public:
  static constexpr uint32_t kClockHz = 10'000'000;
  static_assert(kClockHz <= bmp581::spi::kMaxClockHz, "SCK above the BMP581's maximum");

  bool EnsureInitialized() noexcept {
    if (!ready_) {
      ready_ = platform_spi_init(); // CSB driven high before the first frame
    }
    return ready_;
  }

  // One frame: CSB low, `len` bytes out and in, CSB high. `rx` may be null on a write.
  bool Transfer(const uint8_t* tx, uint8_t* rx, std::size_t len) noexcept {
    return platform_spi_transfer(tx, rx, len);
  }

  void DelayUs(uint32_t us) noexcept {
    platform_delay_us(us);
  }

private:
  bool ready_ = false;
};
// --8<-- [end:spi-port]

#endif // BMP581_MY_PORT_HPP
