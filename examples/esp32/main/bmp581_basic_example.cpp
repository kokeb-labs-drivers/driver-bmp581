/**
 * @file bmp581_basic_example.cpp
 * @brief Initialise the BMP581 from the menuconfig values and log a forced measurement twice a
 * second.
 *
 * Wiring: examples/esp32/README.md. Expected output: one "initialised" line with the ASIC revision,
 * then "101325.000 Pa 21.5000 C" lines. Every Result is checked and logged by name.
 */
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bmp581.hpp"
#include "bmp581_kconfig.hpp"
#include "esp32_bmp581_port.hpp"

namespace {
constexpr const char* kTag = "bmp581_example";
}

extern "C" void app_main(void) {
  static Esp32Bmp581Port port; // static: the driver keeps a reference
  static bmp581::BMP581<Esp32Bmp581Port> sensor(port, bmp581::KconfigDefaults()); // no I/O

  ESP_LOGI(kTag, "BMP581 driver %s", bmp581::GetDriverVersion());
  const auto init = sensor.EnsureInitialized();
  if (!init) {
    ESP_LOGE(kTag, "EnsureInitialized failed: %s", bmp581::ToString(init.error));
    return;
  }
  ESP_LOGI(kTag, "initialised, ASIC revision 0x%02X", sensor.GetRevision());

  while (true) {
    const auto m = sensor.MeasureOnce();
    if (m) {
      ESP_LOGI(kTag, "%.3f Pa %.4f C",
               static_cast<double>(bmp581::PressurePa(m.value.pressure_raw)),
               static_cast<double>(bmp581::TemperatureC(m.value.temperature_raw)));
    } else {
      ESP_LOGW(kTag, "measurement failed: %s", bmp581::ToString(m.error));
      if (m.error == bmp581::Error::DeviceReset) {
        (void)sensor.EnsureInitialized(); // the chip reset: soft reset and configure it again
      }
    }
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}
