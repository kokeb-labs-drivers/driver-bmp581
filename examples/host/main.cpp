/**
 * @file main.cpp
 * @brief The BMP581 driver on your PC: initialise a simulated chip, take forced measurements, then
 * run normal mode with data ready.
 *
 * No hardware and no SDK: the host port (host_bmp581_port.hpp) connects the real driver to the
 * register model the host tests use. The same calls run on an MCU with that MCU's port
 * (examples/esp32/ for ESP-IDF). Exit code 0 when every call succeeded.
 */
#include <cstdint>
#include <cstdio>

#include "bmp581.hpp"
#include "host_bmp581_port.hpp"

namespace {

void Print(const bmp581::Measurement& m) {
  std::printf("%.3f Pa  %.4f C\n", static_cast<double>(bmp581::PressurePa(m.pressure_raw)),
              static_cast<double>(bmp581::TemperatureC(m.temperature_raw)));
}

} // namespace

int main() {
  HostBmp581Port port;
  port.state.chip.pressure_raw = 101'325 * 64; // the simulated air: 101325 Pa, 21.5 C
  port.state.chip.temperature_raw = (43 * 65536) / 2;
  bmp581::BMP581<HostBmp581Port> sensor(port, bmp581::ForcedPreset()); // no I/O yet

  std::printf("BMP581 driver %s on the host\n", bmp581::GetDriverVersion());
  if (const auto r = sensor.EnsureInitialized(); !r) {
    std::printf("EnsureInitialized failed: %s\n", bmp581::ToString(r.error));
    return 1;
  }
  for (int i = 0; i < 2; ++i) {
    const auto m = sensor.MeasureOnce();
    if (!m) {
      std::printf("forced measurement failed: %s\n", bmp581::ToString(m.error));
      return 1;
    }
    Print(m.value);
  }
  if (const auto r = sensor.ApplyConfig(bmp581::LowPowerPreset()); !r) {
    std::printf("ApplyConfig failed: %s\n", bmp581::ToString(r.error));
    return 1;
  }
  for (int i = 0; i < 2; ++i) {
    const auto ready = sensor.WaitDataReady();
    const auto m = ready ? sensor.ReadMeasurement()
                         : bmp581::Result<bmp581::Measurement>::Failure(ready.error);
    if (!m) {
      std::printf("normal mode read failed: %s\n", bmp581::ToString(m.error));
      return 1;
    }
    Print(m.value);
  }
  return sensor.Deinitialize() ? 0 : 1;
}
