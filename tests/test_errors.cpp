/**
 * @file test_errors.cpp
 * @brief The `Error` enum follows the standard, and its values are returned by real calls.
 *
 * - The base set (0 to 9) has the names and values every driver of the org shares.
 * - Every chip-specific value (16 and up) MUST be returned by some call: the enumerators are found
 *   through `ToString()` (-Wswitch keeps it complete), so a new value fails this test until a
 *   scenario below produces it.
 * - Base values MAY go unreturned; the BMP581 driver returns all but `Crc` (the bus carries no
 * CRC), which the API guide lists.
 */
#include <array>
#include <cstring>

#include "fixture.hpp"
#include "test_harness.hpp"

using bmp581::Config;
using bmp581::Error;
using bmp581::PowerMode;
using bmp581_test::FullPort;
using bmp581_test::MinimalPort;
using bmp581_test::MinimalSpiPort;
using bmp581_test::Rig;

static_assert(static_cast<int>(Error::Ok) == 0 && static_cast<int>(Error::NotInitialized) == 1 &&
                  static_cast<int>(Error::InvalidArgument) == 2 &&
                  static_cast<int>(Error::Bus) == 3 && static_cast<int>(Error::Timeout) == 4 &&
                  static_cast<int>(Error::DeviceIdMismatch) == 5 &&
                  static_cast<int>(Error::Crc) == 6 && static_cast<int>(Error::Busy) == 7 &&
                  static_cast<int>(Error::DeviceFault) == 8 &&
                  static_cast<int>(Error::Unsupported) == 9,
              "the base Error set has fixed values (standards/driver.md)");

namespace {

constexpr int kFirstChipSpecific = 16;
/// Base values this driver never returns, each with its reason on the API guide.
constexpr std::array<Error, 1> kNeverReturned{Error::Crc};

// Which Error values some scenario returned; written by Record() while the scenarios run.
std::array<bool, 256> g_seen{}; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

void Record(Error e) {
  g_seen[static_cast<uint8_t>(e)] = true;
}

void BaseScenarios() {
  {
    Rig<MinimalPort> r;
    Record(r.sensor.ReadMeasurement().error);               // NotInitialized
    Record(r.sensor.EnsureInitialized().error);             // Ok
    Record(r.sensor.SetPowerMode(PowerMode::Forced).error); // InvalidArgument
    Record(r.sensor.WaitDataReady().error);                 // Unsupported
  }
  {
    Rig<MinimalPort> r;
    r.Bus().bus_up_ok = false;
    Record(r.sensor.EnsureInitialized().error); // Bus
  }
  {
    Rig<MinimalPort> r;
    r.Chip().nvm_never_ready = true;
    Record(r.sensor.EnsureInitialized().error); // Timeout
  }
  {
    Rig<MinimalPort> r;
    r.Chip().chip_id = 0x51;
    Record(r.sensor.EnsureInitialized().error); // DeviceIdMismatch
  }
  {
    Rig<MinimalPort> r;
    r.Chip().nvm_error = true;
    Record(r.sensor.EnsureInitialized().error); // DeviceFault
  }
  {
    Rig<MinimalPort> r(bmp581::LowPowerPreset());
    (void)r.Start();
    Record(r.sensor.MeasureOnce().error); // Busy
  }
}

void ChipScenarios() {
  {
    Rig<MinimalPort> r;
    (void)r.Start();
    r.Chip().PowerOnReset();
    r.Chip().Elapse(5000);
    Record(r.sensor.CheckForReset().error); // DeviceReset
  }
  {
    Rig<MinimalPort> r;
    (void)r.Start();
    r.Bus().line = bmp581_test::LineFault::FloatHigh;
    r.Bus().line_from = r.Next();
    Record(r.sensor.ReadMeasurement().error); // InvalidReply
  }
  {
    Rig<FullPort> r;
    r.Bus().fail_from = 0;
    Record(r.sensor.EnsureInitialized().error); // NoDevice
  }
  {
    Rig<MinimalPort> r;
    (void)r.Start();
    Record(r.sensor.ReadMeasurement().error); // NoData
    r.Chip().pressure_raw = 0;
    Record(r.sensor.MeasureOnce().error); // OutOfRange
  }
  {
    Rig<MinimalPort> r;
    (void)r.Start();
    r.Chip().ignore_writes = true;
    Record(r.sensor.ApplyConfig(bmp581::HighResolutionPreset()).error); // WriteMismatch
  }
  {
    Rig<MinimalPort> r(bmp581::LowPowerPreset());
    (void)r.Start();
    r.Bus().lost_ack_transfer =
        r.Next() + 2; // the standby write of ApplyConfig: it may have landed
    (void)r.sensor.ApplyConfig(bmp581::LowPowerPreset());
    Record(r.sensor.MeasureOnce().error); // StateUnknown
  }
  {
    Rig<MinimalSpiPort> r(bmp581::LowPowerPreset());
    r.Chip().reject_odr = true;
    Record(r.sensor.EnsureInitialized().error); // ConfigRejected
  }
}

} // namespace

TEST_CASE("Every chip-specific Error value is returned by some call") {
  BaseScenarios();
  ChipScenarios();
  for (int v = kFirstChipSpecific; v < 256; ++v) {
    const auto e = static_cast<Error>(v);
    if (std::strcmp(bmp581::ToString(e), "Unknown") != 0) {
      if (!g_seen[static_cast<std::size_t>(v)]) {
        ::ut::ReportFailureValues(__FILE__, __LINE__, "chip-specific value never returned", v, 0);
      }
    }
  }
}

TEST_CASE("Every base Error value but the listed ones is returned by some call") {
  BaseScenarios();
  for (int v = 0; v < kFirstChipSpecific; ++v) {
    const auto e = static_cast<Error>(v);
    bool exempt = std::strcmp(bmp581::ToString(e), "Unknown") == 0;
    for (const Error never : kNeverReturned) {
      exempt = exempt || (never == e);
    }
    if (!exempt && !g_seen[static_cast<std::size_t>(v)]) {
      ::ut::ReportFailureValues(__FILE__, __LINE__, "base value never returned", v, 0);
    }
  }
}
