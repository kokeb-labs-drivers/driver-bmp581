# Changelog

All notable changes to this repository are listed here. Versions follow [Semantic Versioning](https://semver.org/).
Entries under **Breaking** need source changes in code that used the affected API; any of them calls for a major
version bump (a minor bump before 1.0).

## [Unreleased]

### Changed

- The repository moved to kokeb-labs-drivers; URLs follow.

## [0.1.0] - 2026-11-10

### Added

- **Driver.** `bmp581::BMP581<Port>` for the Bosch BMP581 (datasheet BST-BMP581-DS004-13, revision 1.13), on
  I2C (`I2cInterface`, `I2cPort`) or four-wire SPI (`SpiInterface`, `SpiPort`), the bus chosen by the port type
  (`BusPort`).
- **Start-up.** `EnsureInitialized()`: power-up wait, the SPI selection read, a soft reset, CHIP_ID, NVM ready
  and INT_STATUS = 0x10 then 0x00 (section 4.3.9), then standby with deep standby off (the reset leaves the chip
  in deep standby, where writes need not land, section 4.3), the reset marker in NVM_DATA, and the configuration
  one register per transfer, ODR_CONFIG last, read back in full. `Deinitialize()`: a soft reset into deep
  standby, proved by INT_STATUS. Every soft reset is preceded by standby with deep standby off.
- **Measurements.** `MeasureOnce()` (forced mode, polled until it shows as started, at most t_startup, then polled
  to standby within the datasheet maximum), `SetPowerMode()` (standby, normal, continuous, through standby, read
  back; in normal mode OSR_EFF.odr_is_valid polled too, `ConfigRejected` only at the deadline; with the FIFO on,
  t_reconf for the FIFO flush before `Ok`), `WaitDataReady()`, `ReadMeasurement()` (one 12-byte read of
  TEMP_DATA to STATUS); `PressurePa()`, `TemperatureC()` and `SignExtend24()`, exact.
- **FIFO and interrupts.** `ReadFifo()` with a validity per frame, `ReadFifoCount()`, `ReadInterruptStatus()`, the
  latch (`GetLatchedInterrupts()`), the INT pin, the four sources and the out-of-range window in `Config`.
- **Values.** No frame of a stuck or floating line (reserved registers, reserved bits, all 0x00, all 0xFF), no
  reset value 0x7F7F7F and nothing outside 30 kPa to 125 kPa or -40 °C to 85 °C is returned as a value.
  `Verify::ReadBack` on `ReadMeasurement()` and `MeasureOnce()` reads the data again, so a bit flipped inside a
  data byte is outvoted or refused.
- **Resets.** OSR_CONFIG.press_en (always written 1) is the reset sentinel, read with ODR_CONFIG and OSR_EFF and their
  reserved bits, so no constant byte passes; a marker in NVM_DATA, verified or written in standby before every
  configuration sequence and counted only until the next measurement from deep standby (whose NVM download may overwrite
  it, 0x00 0x00 included), shows a reset that a sequence's own writes covered; INT_STATUS.por, the reset value of the
  data registers, an empty FIFO and an empty or out-of-range FIFO frame run the reset check; on SPI every status, FIFO
  and raw read runs it before returning (the frame that only selects SPI, section 5.1); evidence is confirmed by a
  second read; setters read INT_STATUS and the sentinel before they write; a por the sentinel contradicts is decided by
  the whole configuration and the marker; a check that cannot be read stays pending (`DeviceReset`, `CheckForReset()`).
- **Doubt.** A failed standby or mode write leaves the configuration in doubt (`IsConfigKnown()`,
  `StateUnknown`); a configuration sequence that fails after its first write leaves the driver uninitialised;
  standby and `Deinitialize()` are never refused, also after a failed first start that reached the chip.
- **Configuration.** `Config` with the datasheet reset values, `Validate()` with table 7 and the INT pad drive
  rule of section 7.5 (8 and up with open drain only), `CheckedConfig`, the
  presets `ForcedPreset()`, `LowPowerPreset()`, `HighResolutionPreset()`, `FifoPreset()`, and `KconfigDefaults()`.
- **Tests and docs.** Host tests over a register model that measures and resets on its own and offers each open
  reading of the datasheet as a knob (deep standby losing writes, odr_is_valid and mode lags, the SPI selection
  fill), a fuzz target with the safety rules as invariants and no skips, chip diagrams (pinout, blocks, the I2C
  and SPI connections, a MISO pull-up on SPI), the start-up and measurement figures, and the host and ESP32
  examples.
