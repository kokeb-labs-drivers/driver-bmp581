/**
 * @file bmp581_kconfig.hpp
 * @brief Optional ESP-IDF layer: a `Config` built from the `CONFIG_BMP581_*` Kconfig options.
 * @copyright Copyright (c) 2026 The driver-bmp581 authors. SPDX-License-Identifier: MIT
 *
 * @details
 * Include this header only where you want menuconfig to choose the defaults; the driver never
 * includes it and no driver signature depends on it. Under ESP-IDF the values come from
 * `sdkconfig.h`. Without Kconfig every option falls back to the datasheet default, so
 * `KconfigDefaults()` equals `Config{}`.
 *
 * Kconfig writes a bool option set to "n" as an absent macro, so an absent
 * `CONFIG_BMP581_DATA_READY` means off in both cases.
 *
 * One configuration per program: the inline function below depends on the `CONFIG_` macros, so
 * every translation unit of one program must see the same values (ESP-IDF has one `sdkconfig.h`
 * per build; each host test is its own program). No driver or library header includes this one
 * (handbook KB-CPP20-018).
 */
#ifndef BMP581_KCONFIG_HPP
#define BMP581_KCONFIG_HPP

#ifdef __has_include
#if __has_include("sdkconfig.h")
#include "sdkconfig.h"
#endif
#endif

#include <cstdint>
#include <utility>

#include "bmp581_types.hpp"

#ifndef CONFIG_BMP581_PRESSURE_OVERSAMPLING
/** Pressure oversampling when Kconfig does not set it: 1, the reset value. */
#define CONFIG_BMP581_PRESSURE_OVERSAMPLING 1
#endif
#ifndef CONFIG_BMP581_TEMPERATURE_OVERSAMPLING
/** Temperature oversampling when Kconfig does not set it: 1, the reset value. */
#define CONFIG_BMP581_TEMPERATURE_OVERSAMPLING 1
#endif
#ifndef CONFIG_BMP581_STARTUP_TIMEOUT_MS
/** NVM-ready timeout in milliseconds when Kconfig does not set it: `Config{}.startup_timeout_ms`.
 */
#define CONFIG_BMP581_STARTUP_TIMEOUT_MS 10
#endif
#ifndef CONFIG_BMP581_DATA_READY_TIMEOUT_MS
/** Data-ready timeout in milliseconds when Kconfig does not set it:
 * `Config{}.data_ready_timeout_ms`. */
#define CONFIG_BMP581_DATA_READY_TIMEOUT_MS 2000
#endif

namespace bmp581 {

/**
 * @brief The oversampling setting for a number of samples, rounded down to a power of two.
 * @param samples 1 to 128; below 1 is 1, above 128 is 128.
 * @return The setting.
 */
constexpr Oversampling OversamplingFromSamples(int32_t samples) noexcept {
  uint32_t code = 0U;
  for (uint32_t i = 1U; i < 8U; ++i) {
    if (std::cmp_greater_equal(samples, uint32_t{1} << i)) {
      code = i;
    }
  }
  return static_cast<Oversampling>(code); // 0 to 7: every value is an enumerator
}

/**
 * @brief The configuration menuconfig selected, on top of the datasheet defaults.
 * @return `Config{}` with the oversampling, the data-ready source and the timeouts from Kconfig.
 */
constexpr Config KconfigDefaults() noexcept {
  Config c{};
  c.pressure_oversampling = OversamplingFromSamples(CONFIG_BMP581_PRESSURE_OVERSAMPLING);
  c.temperature_oversampling = OversamplingFromSamples(CONFIG_BMP581_TEMPERATURE_OVERSAMPLING);
#if defined(CONFIG_BMP581_DATA_READY) && CONFIG_BMP581_DATA_READY
  c.source_data_ready = true;
#endif
  c.startup_timeout_ms = CONFIG_BMP581_STARTUP_TIMEOUT_MS;
  c.data_ready_timeout_ms = CONFIG_BMP581_DATA_READY_TIMEOUT_MS;
  return c;
}

static_assert(OversamplingFromSamples(1) == Oversampling::X1);
static_assert(OversamplingFromSamples(100) == Oversampling::X64);
static_assert(OversamplingFromSamples(1000) == Oversampling::X128);

} // namespace bmp581

#endif // BMP581_KCONFIG_HPP
