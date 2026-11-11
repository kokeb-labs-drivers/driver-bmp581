/**
 * @file configuration.cpp
 * @brief The Configuration page's examples, compiled by the host build (never run).
 */
#include "bmp581.hpp"
#include "bmp581_kconfig.hpp"
#include "my_port.hpp"

// --8<-- [start:custom]
constexpr bmp581::Config DroneConfig() {
  bmp581::Config c{};                                 // datasheet reset values
  c.mode = bmp581::PowerMode::Normal;                 // one measurement per ODR period
  c.pressure_oversampling = bmp581::Oversampling::X8; // table 9: 0.30 Pa RMS
  c.odr = bmp581::OutputDataRate::Hz50p1;             // table 7: x8/x1 allows 140 Hz
  c.pressure_iir = bmp581::IirFilter::Coef3;          // table 10
  c.iir_to_data_registers = true;                     // the filtered value in the registers
  c.source_data_ready = true;                         // WaitDataReady() needs it
  c.data_ready_timeout_ms = 50;                       // a little over one period
  return c;                                           // bmp581::Validate(c) checks it
}

bmp581::Error StartDrone(MyPort& port) {
  static bmp581::BMP581<MyPort> sensor(port, DroneConfig()); // stored, no I/O
  return sensor.EnsureInitialized().error; // InvalidArgument if the config is out of range
}
// --8<-- [end:custom]

// --8<-- [start:checked]
// Fails to build, naming ConfigIsInvalid, if Validate() rejects it (table 7, the ranges).
constexpr bmp581::CheckedConfig kBoard{DroneConfig()};
// --8<-- [end:checked]

// --8<-- [start:kconfig]
bmp581::Error StartFromMenuconfig(MyPort& port) {
  static bmp581::BMP581<MyPort> sensor(port, bmp581::KconfigDefaults());
  return sensor.EnsureInitialized().error;
}
// --8<-- [end:kconfig]

bmp581::Error StartBoard(MyPort& port) {
  static bmp581::BMP581<MyPort> sensor(port, kBoard.Get());
  return sensor.EnsureInitialized().error;
}
