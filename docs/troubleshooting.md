# Troubleshooting

Symptoms, causes and fixes, cheapest check first.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From Bosch Sensortec BST-BMP581-DS004-13 1.13:

    - Normal mode and the ODR/OSR check: 4.3.4 NORMAL mode; Table 7 maximum nominal ODR setting per OSR settings, [p. 17](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=17)
    - Post-power-up checks: 4.3.9 Post-power-up procedure, [p. 18](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=18)
    - I2C protocol and addresses: 5.6 I2C protocol; 5.6.1 I2C write operation; 5.6.2 I2C read operation, [p. 38](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=38)
    - Identification and status: 7.1 Register (0x01) ASIC identification ID; 7.22 Register (0x28) Status register, [p. 51](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=51)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

## EnsureInitialized returns NoDevice or Bus

The port reported a failure: its `EnsureInitialized()` returned `false`, or a transfer did.

1. Check that your port's `EnsureInitialized()` returns `true` on its own (log it).
2. I2C: check the address against SDO (`0x46` with SDO at VSS, `0x47` at VDDIO), the pull-ups, VDD and VDDIO, and
   that CSB is tied to VDDIO. One NACK during the start-up is expected: the soft reset write (section 7.36).
3. SPI: check the CSB pin and that the port drives it high between frames.
4. With a logic analyser, check that the first frame (SPI: a CHIP_ID read; I2C: CMD = 0xB6) appears on the bus.

## EnsureInitialized returns DeviceIdMismatch

CHIP_ID did not read 0x50, or INT_STATUS did not read 0x10 after the soft reset and 0x00 after that.

| Symptom (`ReadRegister()` or a logic analyser) | Likely cause | Fix |
| --- | --- | --- |
| CHIP_ID `0x00` | SDA or MISO stuck low; on SPI no MISO wire, or a chip still in I2C mode | Check continuity; check that CSB toggles |
| CHIP_ID `0xFF` | The data line floating high, or the chip unpowered | Check VDD, VDDIO and SDO |
| CHIP_ID `0x51` or another value | Another part (a BMP585 reads 0x51) | Check the part marking: "C" is a BMP581 (table 29) |
| CHIP_ID `0x50`, INT_STATUS not `0x10` | The soft reset did not reach the chip | SPI: check the first frames; I2C: check that the port sends a write even when a NACK follows |

## EnsureInitialized returns Timeout or DeviceFault

`Timeout`: STATUS.status_nvm_rdy never read 1 within `Config::startup_timeout_ms` after the soft reset; check the
supplies and their ramp (section 4.2). `DeviceFault`: STATUS reports an NVM error, so the trimming data did not
load; a power cycle may clear it, a repeated one means a damaged part.

## EnsureInitialized or ApplyConfig returns WriteMismatch

A configuration register read back differently from what was written. A single one points at noise on the bus:
retry. Every time, on the same register, points at a port that corrupts writes, or at a chip outside standby (the
driver always writes in standby, section 4.3.8).

## SetPowerMode or EnsureInitialized returns ConfigRejected

Normal mode with a data rate the chip does not accept for the oversampling (table 7): OSR_EFF.odr_is_valid still
read 0 at the deadline (t_startup_deep, 4 ms, polled every 0.5 ms), and the chip would measure with its own
defaults. `Validate()` refuses the combinations table 7 rules out, so this means the chip disagrees with the table:
lower the data rate or the oversampling, and read `GetLastOsrEff()` for what the chip chose.

## Reads return InvalidReply

The reply cannot come from the chip: the reserved registers 0x23 to 0x26 did not read 0x00, a reserved bit was
set, or the frame was all 0x00. Once now and then: noise; the next call reads again. Every time: a stuck or
floating line (pull-ups, SDO, the MISO wire). On SPI the first frame after a power-on reset only selects SPI again
and reads as garbage; the driver then checks for the reset itself and returns `DeviceReset`. A pull-up on MISO
makes that frame read 0xFF ([Hardware](hardware.md#spi)).

## Reads return DeviceReset

The chip reset since the driver started it, usually a dip of VDD or VDDIO, confirmed by OSR_CONFIG reading 0x00
twice. The driver is uninitialised: call `EnsureInitialized()`. If it repeats, check the supplies, their
decoupling (100 nF at each pin) and the inrush current (section 6.2.5). A line stuck low reads the same way;
`EnsureInitialized()` then fails with `DeviceIdMismatch`.

## Reads return NoData

The data registers hold their reset value 0x7F7F7F: no measurement since the last reset. Start one:
`MeasureOnce()`, or normal or continuous mode. The driver also checks for a reset first.

## Reads return OutOfRange

The value lies outside 30 kPa to 125 kPa or -40 °C to 85 °C (tables 1 and 2). In a pressure chamber or a freezer
that is the truth; otherwise compare with a reference before trusting the chip.

## MeasureOnce returns Busy or Timeout

`Busy`: the configured mode is normal or continuous, or the previous forced measurement still runs; call
`SetPowerMode(PowerMode::Standby)` first. `Timeout`: the chip did not return to standby within
`ForcedMeasurementMaxUs()` (106 ms at x128 and x128). Check that your port's `DelayUs()` really waits: a delay
that returns at once makes every wait far too short.

## WaitDataReady returns Unsupported or Timeout

`Unsupported`: `Config::source_data_ready` is off (the chip reports drdy only for an enabled source), or the mode
is standby. `Timeout`: no measurement within `data_ready_timeout_ms`; it must cover one data rate period (8 s at
0.125 Hz).

## The pressure jumps when the air moves

That is the air: doors, wind and fans move the pressure by more than the sensor's noise (section 4.4.2). Enable the
IIR filter (`pressure_iir`, `iir_to_data_registers`) and choose the coefficient from table 10.
