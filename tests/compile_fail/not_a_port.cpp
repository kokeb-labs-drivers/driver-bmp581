/**
 * @file not_a_port.cpp
 * @brief Must NOT compile: the driver is constrained by the port concept.
 *
 * EXPECT: Port
 */
#include "bmp581.hpp"

struct NotAPort {
  bool EnsureInitialized() noexcept {
    return true;
  }
};

int main() {
  NotAPort port;
  bmp581::BMP581<NotAPort> sensor(port);
  return static_cast<int>(sensor.EnsureInitialized().error);
}
