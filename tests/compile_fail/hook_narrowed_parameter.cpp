/**
 * @file hook_narrowed_parameter.cpp
 * @brief Must NOT compile: a WriteRead hook with a narrower length would truncate reads, so the
 * port concept rejects it (handbook KB-CPP20-006).
 *
 * EXPECT: I2cPort
 */
#include <cstddef>
#include <cstdint>

#include "bmp581.hpp"

class NarrowPort : public bmp581::I2cInterface<NarrowPort> {
public:
  bool EnsureInitialized() noexcept {
    return true;
  }
  bool Write(uint8_t addr, const uint8_t* data, std::size_t len) noexcept {
    (void)addr;
    (void)data;
    return len > 0U;
  }
  bool WriteRead(uint8_t addr, const uint8_t* tx, uint8_t tx_len, uint8_t* rx,
                 std::size_t rx_len) noexcept {
    (void)addr;
    (void)tx;
    (void)rx;
    return tx_len > 0U && rx_len > 0U;
  }
  void DelayUs(uint32_t us) noexcept {
    (void)us;
  }
};

int main() {
  NarrowPort port;
  bmp581::BMP581<NarrowPort> sensor(port);
  return static_cast<int>(sensor.EnsureInitialized().error);
}
