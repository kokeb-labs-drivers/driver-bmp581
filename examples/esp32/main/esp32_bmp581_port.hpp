/**
 * @file esp32_bmp581_port.hpp
 * @brief ESP-IDF port for the BMP581 driver: i2c_master for the bus, esp_rom delays and ESP_LOG
 * for the log hook.
 *
 * Start from this file for any ESP-IDF target. The pins are in Esp32Bmp581Pins; the defaults match
 * the wiring table in examples/esp32/README.md (ESP32-C6 DevKitC-1). The BMP581 has no reset or
 * enable pin: CSB is tied to VDDIO for I2C (section 6.2.3), so the port drives no control pin.
 *
 * The soft reset in EnsureInitialized() is a write the chip does not acknowledge over I2C (section
 * 7.36); i2c_master reports that NACK in its own log, and the driver expects it.
 */
#ifndef BMP581_ESP32_BMP581_PORT_HPP
#define BMP581_ESP32_BMP581_PORT_HPP

#include <cstddef>
#include <cstdint>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

#include "bmp581.hpp"

/// The port's default SCL clock, checked against the driver's bus requirements (KB-CPP-123).
inline constexpr uint32_t kEsp32Bmp581ClockHz = 400'000;
static_assert((kEsp32Bmp581ClockHz >= bmp581::i2c::kMinClockHz) &&
                  (kEsp32Bmp581ClockHz <= bmp581::i2c::kMaxClockHz),
              "SCL outside the BMP581's range");

/// Pins and bus settings of the ESP-IDF port.
struct Esp32Bmp581Pins {
  i2c_port_num_t port = I2C_NUM_0;
  int sda = 6;
  int scl = 7;
  uint32_t clock_hz = kEsp32Bmp581ClockHz; ///< 100 kHz to `bmp581::i2c::kMaxClockHz`.
  int timeout_ms = 10;                     ///< Per transaction.
};

/// The BMP581 I2C port on ESP-IDF, with the log hook.
class Esp32Bmp581Port : public bmp581::I2cInterface<Esp32Bmp581Port> {
public:
  explicit Esp32Bmp581Port(const Esp32Bmp581Pins& pins = {}) noexcept : pins_(pins) {}

  bool EnsureInitialized() noexcept {
    if (bus_ != nullptr) {
      return true;
    }
    i2c_master_bus_config_t bus{};
    bus.i2c_port = pins_.port;
    bus.sda_io_num = static_cast<gpio_num_t>(pins_.sda);
    bus.scl_io_num = static_cast<gpio_num_t>(pins_.scl);
    bus.clk_source = I2C_CLK_SRC_DEFAULT;
    bus.glitch_ignore_cnt = 7;
    bus.flags.enable_internal_pullup = true; // weak: fit external pull-ups for 400 kHz
    if (i2c_new_master_bus(&bus, &bus_) != ESP_OK) {
      bus_ = nullptr;
      return false;
    }
    return true;
  }

  bool Write(uint8_t addr, const uint8_t* data, std::size_t len) noexcept {
    i2c_master_dev_handle_t dev = device(addr);
    return (dev != nullptr) && (i2c_master_transmit(dev, data, len, pins_.timeout_ms) == ESP_OK);
  }

  bool WriteRead(uint8_t addr, const uint8_t* tx, std::size_t tx_len, uint8_t* rx,
                 std::size_t rx_len) noexcept {
    i2c_master_dev_handle_t dev = device(addr);
    return (dev != nullptr) &&
           (i2c_master_transmit_receive(dev, tx, tx_len, rx, rx_len, pins_.timeout_ms) == ESP_OK);
  }

  void DelayUs(uint32_t us) noexcept {
    esp_rom_delay_us(us);
  }

  void Log(bmp581::LogLevel level, const char* message) noexcept {
    esp_log_level_t esp_level = ESP_LOG_DEBUG;
    if (level == bmp581::LogLevel::Error) {
      esp_level = ESP_LOG_ERROR;
    } else if (level == bmp581::LogLevel::Warning) {
      esp_level = ESP_LOG_WARN;
    } else if (level == bmp581::LogLevel::Info) {
      esp_level = ESP_LOG_INFO;
    } else {
      // Debug stays ESP_LOG_DEBUG.
    }
    esp_log_write(esp_level, kTag, "%s\n", message); // plain text, never a format
  }

private:
  // The device handle for one 7-bit address, created on first use (one chip per port in this
  // example).
  i2c_master_dev_handle_t device(uint8_t addr) noexcept {
    if ((device_ != nullptr) && (device_addr_ == addr)) {
      return device_;
    }
    if (device_ != nullptr) {
      (void)i2c_master_bus_rm_device(device_);
      device_ = nullptr;
    }
    i2c_device_config_t cfg{};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address = addr;
    cfg.scl_speed_hz = pins_.clock_hz;
    if (i2c_master_bus_add_device(bus_, &cfg, &device_) != ESP_OK) {
      device_ = nullptr;
      return nullptr;
    }
    device_addr_ = addr;
    return device_;
  }

  static constexpr const char* kTag = "bmp581";
  Esp32Bmp581Pins pins_;
  i2c_master_bus_handle_t bus_ = nullptr;
  i2c_master_dev_handle_t device_ = nullptr;
  uint8_t device_addr_ = 0;
};

#endif // BMP581_ESP32_BMP581_PORT_HPP
