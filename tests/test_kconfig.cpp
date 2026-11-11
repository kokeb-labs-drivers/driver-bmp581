/**
 * @file test_kconfig.cpp
 * @brief KconfigDefaults(): the defaults without Kconfig, and menuconfig values when they are set.
 *
 * Built twice: without CONFIG_ macros (KCONFIG_SET undefined) and with the values an sdkconfig.h
 * would define (KCONFIG_SET=1, see tests/CMakeLists.txt).
 */
#include "bmp581_kconfig.hpp"
#include "test_harness.hpp"

#ifdef KCONFIG_SET
TEST_CASE("Kconfig values reach the Config") {
  constexpr bmp581::Config kC = bmp581::KconfigDefaults();
  CHECK_EQ(kC.pressure_oversampling, bmp581::Oversampling::X16);
  CHECK_EQ(kC.temperature_oversampling, bmp581::Oversampling::X2); // 3 rounds down to 2
  CHECK(kC.source_data_ready);
  CHECK_EQ(kC.startup_timeout_ms, 20U);
  CHECK_EQ(kC.data_ready_timeout_ms, 250U);
  CHECK_EQ(kC.mode, bmp581::PowerMode::Standby); // not a Kconfig option: datasheet default
  CHECK_EQ(bmp581::Validate(kC), bmp581::Error::Ok);
}
#else
TEST_CASE("Without Kconfig, KconfigDefaults() equals Config{}") {
  constexpr bmp581::Config kC = bmp581::KconfigDefaults();
  CHECK(kC == bmp581::Config{});
  CHECK_EQ(bmp581::OversamplingFromSamples(0), bmp581::Oversampling::X1);
  CHECK_EQ(bmp581::OversamplingFromSamples(128), bmp581::Oversampling::X128);
}
#endif
