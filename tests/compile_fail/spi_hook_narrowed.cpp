/**
 * @file spi_hook_narrowed.cpp
 * @brief Must NOT compile: an SPI Transfer hook with a narrower length would truncate a FIFO burst,
 * so the port concept rejects it (handbook KB-CPP20-006).
 *
 * EXPECT: SpiPort
 */
#include <cstdint>

#include "bmp581.hpp"

class NarrowSpiPort : public bmp581::SpiInterface<NarrowSpiPort> {
public:
  bool EnsureInitialized() noexcept {
    return true;
  }
  bool Transfer(const uint8_t* tx, uint8_t* rx, uint8_t len) noexcept {
    return (tx != nullptr) && (rx != nullptr) && (len > 0U);
  }
  void DelayUs(uint32_t us) noexcept {
    (void)us;
  }
};

int main() {
  NarrowSpiPort port;
  bmp581::BMP581<NarrowSpiPort> sensor(port);
  return static_cast<int>(sensor.EnsureInitialized().error);
}
