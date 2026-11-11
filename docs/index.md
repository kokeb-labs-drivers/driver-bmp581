---
hide:
  - navigation
  - toc
---

<div class="kb-hero" markdown>
<span class="kb-kicker">Driver</span>

# BMP581 Driver

<p>A C++20 driver for the Bosch BMP581 barometric pressure and temperature sensor, on I2C or SPI. It starts the
chip from a soft reset, measures on request or continuously, reads the FIFO, and never returns a stuck line, a
reset value or an out-of-range reading as a measurement, on any MCU.</p>

[Quickstart](quickstart.md){ .md-button .md-button--primary }
[API guide](api-guide.md){ .md-button }
[Source on GitHub](https://github.com/kokeb-labs-drivers/driver-bmp581){ .md-button }
</div>

The driver has no platform code. You give it a small class that brings the bus up and moves bytes; it does the
rest: the soft reset and the post-power-up checks, the configuration written in standby and read back, the
power modes, the data and FIFO reads, the conversion to pascals and degrees Celsius, and the reset check.

<div class="kb-chip" markdown>
<div class="kb-chip__head">
<span class="kb-chip__kicker">Barometric pressure sensor</span>
<span class="kb-chip__name">BMP581</span>
<span class="kb-chip__vendor">Bosch Sensortec</span>
</div>

Function
:   Absolute pressure, 30 kPa to 125 kPa at 1/64 Pa per count; temperature, -40 °C to 85 °C at 1/65536 °C;
    compensated on the chip

Interface
:   I2C up to 1 MHz at `0x46` or `0x47` (SDO), or four-wire SPI up to 12 MHz, mode 0 or 3; I3C (not used here)

Modes
:   Standby and deep standby; forced (one measurement); normal at 0.125 Hz to 240 Hz; continuous

Data
:   Oversampling x1 to x128, an IIR filter, a 32-frame FIFO, interrupts for data ready, FIFO full and threshold,
    and pressure out of range

Supply
:   VDD 1.71 V to 3.6 V, VDDIO 1.08 V to 3.6 V; 1.3 µA at 1 Hz in low-power mode
</div>


## What you get

- **One object per chip, on either bus.** `BMP581<YourPort>` takes a reference to an I2C or an SPI port and does
  no I/O until `EnsureInitialized()`. See [API guide](api-guide.md).
- **Values you can trust.** One 12-byte read per measurement, checked for stuck and floating lines, the reset
  value and the measurement range before a value is returned; FIFO frames carry their own validity. See
  [Contract](contract.md).
- **Resets found, never written over.** A brown-out mid-session is found by the next read or setter, confirmed
  by a second read, and reported as `DeviceReset`; nothing cached is written over the reset chip. See
  [Contract](contract.md#resets).
- **Datasheet defaults.** `Config{}` holds the chip's reset values with units in the field names, plus four
  presets; `Validate()` checks the ranges and the data rate table. See [Configuration](configuration.md).
- **Zero-overhead portability.** The port is a CRTP class checked by a concept: the bus hooks and a delay, plus
  optional failure and logging hooks found at compile time. See [Porting](porting.md).
- **No heap, no exceptions, no RTTI.** The host tests build with `-fno-exceptions -fno-rtti`. See
  [Contract](contract.md).
- **Tested without hardware.** Host tests and a fuzzer run the driver against a register model of the chip that
  measures, resets and fails on its own. See [Testing and CI](testing.md).

## Datasheet

<!-- generated: datasheet-card -->
| Document | Revision | Get it |
| --- | --- | --- |
| Datasheet: BMP581 Barometric Pressure Sensor (Bosch Sensortec BST-BMP581-DS004-13) | 1.13, 2025-04 | [PDF](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf) · [vendor page](https://www.bosch-sensortec.com/products/environmental-sensors/pressure-sensors/bmp581/) |

Written against these revisions; the SHA-256 of each file is in [`docs/datasheets.yml`](datasheets.yml). Where each part
of the driver is in the datasheet: [Datasheet map](datasheet.md).
<!-- /generated -->

## Where to go next

<div class="grid cards" markdown>

-   :material-rocket-launch-outline: **[Quickstart](quickstart.md)**

    ---

    Wire the chip, write the port, take a measurement in about 15 lines.

-   :material-chip: **[Hardware](hardware.md)**

    ---

    Pins, connection, bus settings, timing and power.

-   :material-file-document-check-outline: **[Contract](contract.md)**

    ---

    What the driver promises, what it needs, resets, threading and errors.

-   :material-code-braces: **[API guide](api-guide.md)**

    ---

    Initialise, measure, stream, read the FIFO and handle errors, task by task.

-   :material-swap-horizontal: **[Porting](porting.md)**

    ---

    Implement the I2C or SPI port for your MCU.

-   :material-book-open-variant: **[API reference](api-reference.md)**

    ---

    Every class and member, generated from the headers.

</div>
