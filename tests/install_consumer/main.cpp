/**
 * @file main.cpp
 * @brief Builds the driver from the installed package only: headers, version header and .ipp.
 */
#include <cstddef>
#include <cstdint>

#include "bmp581.hpp"

namespace {

// A port that answers every read with 0: enough to instantiate the whole driver template.
class NullPort : public bmp581::I2cInterface<NullPort> {
public:
  bool EnsureInitialized() noexcept {
    return true;
  }
  bool Write(uint8_t addr, const uint8_t* data, std::size_t len) noexcept {
    (void)addr;
    (void)data;
    (void)len;
    return true;
  }
  bool WriteRead(uint8_t addr, const uint8_t* tx, std::size_t tx_len, uint8_t* rx,
                 std::size_t rx_len) noexcept {
    (void)addr;
    (void)tx;
    (void)tx_len;
    for (std::size_t i = 0; i < rx_len; ++i) {
      rx[i] = 0;
    }
    return true;
  }
  void DelayUs(uint32_t us) noexcept {
    (void)us;
  }
};

} // namespace

int main() {
  NullPort port;
  bmp581::BMP581<NullPort> sensor(port);
  // CHIP_ID reads 0, so EnsureInitialized() must fail with DeviceIdMismatch: proof the template
  // compiled and ran.
  const auto r = sensor.EnsureInitialized();
  return (r.error == bmp581::Error::DeviceIdMismatch && bmp581::GetDriverVersion()[0] != '\0') ? 0
                                                                                               : 1;
}
