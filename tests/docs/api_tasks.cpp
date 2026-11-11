/**
 * @file api_tasks.cpp
 * @brief The API guide's examples, compiled by the host build (never run).
 */
#include <array>
#include <cstdio>

#include "bmp581.hpp"
#include "my_port.hpp"

using Sensor = bmp581::BMP581<MyPort>;

// --8<-- [start:lifecycle]
bmp581::Error BringUp(Sensor& sensor) {
  // The one entry point: starts the chip on the first call; later calls run the reset check and
  // start again when the chip reset. After a failure the next call starts from the beginning.
  const auto r = sensor.EnsureInitialized();
  if (!r) {
    std::printf("BMP581 init: %s\n", bmp581::ToString(r.error));
  }
  return r.error;
}
// --8<-- [end:lifecycle]

// --8<-- [start:forced]
bmp581::Result<bmp581::Measurement> SampleOnDemand(Sensor& sensor) {
  // Standby between calls (deep standby with Config{}); one forced measurement per call.
  return sensor.MeasureOnce(); // reset check, forced, poll until standby, one 12-byte read
}
// --8<-- [end:forced]

// --8<-- [start:normal]
bmp581::Error StreamAt25Hz(Sensor& sensor, int samples) {
  // HighResolutionPreset(): normal mode, 25 Hz, x16 pressure, IIR, data-ready source on.
  if (auto r = sensor.ApplyConfig(bmp581::HighResolutionPreset()); !r) {
    return r.error;
  }
  for (int i = 0; i < samples; ++i) {
    if (auto r = sensor.WaitDataReady(); !r) {
      return r.error; // Timeout, or DeviceReset: call EnsureInitialized()
    }
    const auto m = sensor.ReadMeasurement();
    if (!m) {
      return m.error;
    }
    std::printf("%.3f Pa\n", static_cast<double>(bmp581::PressurePa(m.value.pressure_raw)));
  }
  return bmp581::Error::Ok;
}
// --8<-- [end:normal]

// --8<-- [start:fifo]
std::size_t DrainFifo(Sensor& sensor, std::span<bmp581::FifoSample> out) {
  // FifoPreset(): pressure-and-temperature frames at 10 Hz, threshold at 8 frames.
  const auto n = sensor.ReadFifo(out);
  if (!n) {
    return 0; // InvalidReply (a stuck line) or DeviceReset
  }
  std::size_t good = 0;
  for (std::size_t i = 0; i < n.value; ++i) {
    good += (out[i].error == bmp581::Error::Ok) ? 1U : 0U; // each frame has its own validity
  }
  return good;
}
// --8<-- [end:fifo]

// --8<-- [start:reset]
bmp581::Error Supervise(Sensor& sensor) {
  // A brown-out resets the chip to standby with every register at its reset value. Every data
  // read checks for it; CheckForReset() does it alone (one 2-byte read).
  const auto r = sensor.CheckForReset();
  if (r.error == bmp581::Error::DeviceReset) {
    return sensor.EnsureInitialized().error; // soft reset and the configuration, again
  }
  return r.error;
}
// --8<-- [end:reset]

// --8<-- [start:errors]
const char* Describe(bmp581::Error e) {
  switch (e) {
  case bmp581::Error::NoDevice:
  case bmp581::Error::Bus:
  case bmp581::Error::DeviceIdMismatch:
    return "check the wiring, CSB and SDO, and the address";
  case bmp581::Error::InvalidReply:
    return "a line is stuck or floating: check pull-ups and SDO";
  case bmp581::Error::DeviceReset:
    return "the chip reset: call EnsureInitialized()";
  case bmp581::Error::OutOfRange:
    return "outside 30 to 125 kPa or -40 to 85 degrees C";
  default:
    return bmp581::ToString(e);
  }
}
// --8<-- [end:errors]

// --8<-- [start:deinit]
void PowerDown(Sensor& sensor) {
  // One byte, CMD = 0xB6: deep standby with the reset values, proved by INT_STATUS.por.
  if (auto r = sensor.Deinitialize(); !r) {
    std::printf("BMP581 soft reset not proved: %s\n", bmp581::ToString(r.error));
  }
}
// --8<-- [end:deinit]
