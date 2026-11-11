/**
 * @file quickstart.cpp
 * @brief The Quickstart page's example, compiled by the host build (never run).
 */
#include "my_port.hpp"

// --8<-- [start:quickstart]
#include "bmp581.hpp"

int read_pressure(MyPort& port, float& pascals, float& celsius) {
  bmp581::BMP581<MyPort> sensor(port, bmp581::ForcedPreset()); // no I/O yet

  if (auto r = sensor.EnsureInitialized(); !r) { // 1. soft reset, CHIP_ID, NVM ready, Config
    return static_cast<int>(r.error); //    NoDevice, Bus or DeviceIdMismatch: check wiring
  }
  const auto m = sensor.MeasureOnce(); // 2. one forced measurement, then standby
  if (!m) {
    return static_cast<int>(m.error); //    every failure is an Error, never a number
  }
  pascals = bmp581::PressurePa(m.value.pressure_raw);      // 3. 1/64 Pa, exact
  celsius = bmp581::TemperatureC(m.value.temperature_raw); //    1/65536 °C, exact
  return 0;
}
// --8<-- [end:quickstart]
