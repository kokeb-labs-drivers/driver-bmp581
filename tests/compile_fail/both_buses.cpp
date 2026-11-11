/**
 * @file both_buses.cpp
 * @brief Must NOT compile: a port that is both an I2C and an SPI port leaves the framing
 * ambiguous, so the driver's `BusPort` concept rejects it.
 *
 * EXPECT: BusPort
 */
#include <cstddef>
#include <cstdint>

#include "bmp581.hpp"

class TwoBusPort : public bmp581::I2cInterface<TwoBusPort>,
                   public bmp581::SpiInterface<TwoBusPort> {
public:
  bool EnsureInitialized() noexcept {
    return true;
  }
  bool Write(uint8_t addr, const uint8_t* data, std::size_t len) noexcept {
    return (addr != 0U) && (data != nullptr) && (len > 0U);
  }
  bool WriteRead(uint8_t addr, const uint8_t* tx, std::size_t tx_len, uint8_t* rx,
                 std::size_t rx_len) noexcept {
    return (addr != 0U) && (tx != nullptr) && (tx_len > 0U) && (rx != nullptr) && (rx_len > 0U);
  }
  bool Transfer(const uint8_t* tx, uint8_t* rx, std::size_t len) noexcept {
    return (tx != nullptr) && (rx != nullptr) && (len > 0U);
  }
  void DelayUs(uint32_t us) noexcept {
    (void)us;
  }
};

int main() {
  TwoBusPort port;
  bmp581::BMP581<TwoBusPort> sensor(port);
  return static_cast<int>(sensor.EnsureInitialized().error);
}
