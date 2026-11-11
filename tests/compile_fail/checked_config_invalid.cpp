/**
 * @file checked_config_invalid.cpp
 * @brief Must NOT compile: a CheckedConfig built from a configuration that Validate() rejects fails
 * the build, naming the error (handbook KB-CPP20-013).
 *
 * EXPECT: ConfigIsInvalid
 */
#include "bmp581_types.hpp"

int main() {
  // Normal mode at 240 Hz with x128 pressure oversampling: table 7 allows 10 Hz.
  constexpr bmp581::CheckedConfig kBoard{
      bmp581::Config{.mode = bmp581::PowerMode::Normal,
                     .pressure_oversampling = bmp581::Oversampling::X128,
                     .odr = bmp581::OutputDataRate::Hz240}};
  return static_cast<int>(kBoard.Get().odr);
}
