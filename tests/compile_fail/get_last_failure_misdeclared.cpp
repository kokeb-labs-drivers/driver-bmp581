/**
 * @file get_last_failure_misdeclared.cpp
 * @brief Must NOT compile: an optional hook with the wrong signature is rejected, not ignored
 * (handbook KB-CPP20-007).
 *
 * EXPECT: GetLastFailure hook must be
 */
#include "bmp581.hpp"
#include "mock_bmp581.hpp"

class ValueFailurePort : public bmp581_test::MockPortBase<ValueFailurePort> {
public:
  void DelayUs(uint32_t us) noexcept {
    (void)us;
  }
  bool GetLastFailure(bmp581::I2cFailure failure) noexcept { // by value: could never be set
    return failure == bmp581::I2cFailure::Other;
  }
};

int main() {
  ValueFailurePort port;
  bmp581::BMP581<ValueFailurePort> sensor(port);
  return static_cast<int>(sensor.EnsureInitialized().error);
}
