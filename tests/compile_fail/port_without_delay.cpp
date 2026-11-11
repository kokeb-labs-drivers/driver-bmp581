/**
 * @file port_without_delay.cpp
 * @brief Must NOT compile: the chip's reset and conversion waits need the port's DelayUs().
 *
 * EXPECT: DelayUs
 */
#include "bmp581.hpp"
#include "mock_bmp581.hpp"

class NoDelayPort : public bmp581_test::MockPortBase<NoDelayPort> {};

int main() {
  NoDelayPort port;
  bmp581::BMP581<NoDelayPort> sensor(port);
  return static_cast<int>(sensor.EnsureInitialized().error);
}
