# Figures

<!-- Written by kokeb-docs figures from docs/diagrams/figures/*.py. Do not edit by hand. -->

Every figure on this site is drawn by a Python module from facts extracted from the code, so it is rebuilt,
not redrawn, when the code changes. CI runs `kokeb-docs figures --check` and fails when a
committed figure no longer matches the code. Each figure below lists the files it is drawn from.

## Inside the BMP581

<!-- figure: chip-blocks -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-blocks.dark.svg">
  <img src="chip-blocks.svg"
    alt="Block diagram of the BMP581: its function blocks, how they connect and which package pins reach each
    block.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/bmp581_i2c_interface.hpp`](../../inc/bmp581_i2c_interface.hpp)
- [`inc/bmp581_spi_interface.hpp`](../../inc/bmp581_spi_interface.hpp)

## BMP581: I2C connection

<!-- figure: chip-circuit-i2c -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-circuit-i2c.dark.svg">
  <img src="chip-circuit-i2c.svg"
    alt="Schematic of the BMP581 i2c connection: the chip with U2 MCU, R1 resistor 4.7 kΩ, R2 resistor 4.7
    kΩ, C1 capacitor 100 nF, C2 capacitor 100 nF; the MCU wires carry the port hooks Transfer(),
    WriteRead() or Transfer().">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/bmp581_i2c_interface.hpp`](../../inc/bmp581_i2c_interface.hpp)
- [`inc/bmp581_spi_interface.hpp`](../../inc/bmp581_spi_interface.hpp)

## BMP581: SPI 4-wire connection

<!-- figure: chip-circuit-spi4 -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-circuit-spi4.dark.svg">
  <img src="chip-circuit-spi4.svg"
    alt="Schematic of the BMP581 spi 4-wire connection: the chip with U2 MCU, R1 resistor 47 kΩ, C1 capacitor
    100 nF, C2 capacitor 100 nF; the MCU wires carry the port hooks Transfer(), WriteRead() or
    Transfer().">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/bmp581_i2c_interface.hpp`](../../inc/bmp581_i2c_interface.hpp)
- [`inc/bmp581_spi_interface.hpp`](../../inc/bmp581_spi_interface.hpp)

## BMP581 pinout, LGA-10

<!-- figure: chip-pinout -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-pinout.dark.svg">
  <img src="chip-pinout.svg"
    alt="Pinout of the BMP581 in LGA-10, top view: 10 pins, each with its name, its electrical type as a
    colour and the driver hook that uses it.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/bmp581_i2c_interface.hpp`](../../inc/bmp581_i2c_interface.hpp)
- [`inc/bmp581_spi_interface.hpp`](../../inc/bmp581_spi_interface.hpp)

## The layers, one of them yours

<!-- figure: driver-layers -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="driver-layers.dark.svg">
  <img src="driver-layers.svg"
    alt="Layer diagram: your application calls the BMP581 core, which sends register reads and writes through
    the I2C or SPI port contract to the port class you write, which drives the chip; the core and the
    contract are the same on every MCU.">
</picture>
<!-- /figure -->

Figure module: [`docs/diagrams/figures/driver-layers.py`](../../docs/diagrams/figures/driver-layers.py)

Drawn from:

- `public calls` in [`inc/bmp581.hpp`](../../inc/bmp581.hpp)
- `I2cInterface` in [`inc/bmp581_i2c_interface.hpp`](../../inc/bmp581_i2c_interface.hpp)
- `SpiInterface` in [`inc/bmp581_spi_interface.hpp`](../../inc/bmp581_spi_interface.hpp)

## From a conversion to a value

<!-- figure: measurement -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="measurement.dark.svg">
  <img src="measurement.svg"
    alt="Flow chart: MeasureOnce() or WaitDataReady() leads to one 12-byte read of TEMP_DATA to STATUS, then
    the checks: reserved registers and stuck lines, the reset flag, the reset value, the measurement
    range; each failing check returns its error, and only a frame that passes them all is a value.">
</picture>
<!-- /figure -->

Figure module: [`docs/diagrams/figures/measurement.py`](../../docs/diagrams/figures/measurement.py)

Drawn from:

- `MeasureOnce, waitForcedShown, WaitDataReady, readData, settle` in [`src/bmp581.ipp`](../../src/bmp581.ipp)
- `kDataReset` in [`inc/bmp581_registers.hpp`](../../inc/bmp581_registers.hpp)
- `kPressureMinPa, kPressureMaxPa, kTemperatureMinC, kTemperatureMaxC` in [`inc/bmp581_types.hpp`](../../inc/bmp581_types.hpp)

## What EnsureInitialized() does

<!-- figure: start-up -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="start-up.dark.svg">
  <img src="start-up.svg"
    alt="Flow chart of EnsureInitialized(): check the configuration and the address, wait the power-up time,
    soft reset, check CHIP_ID, NVM ready and the reset flag, write and read back the configuration,
    enter the power mode; each failing step returns its error.">
</picture>
<!-- /figure -->

Figure module: [`docs/diagrams/figures/start-up.py`](../../docs/diagrams/figures/start-up.py)

Drawn from:

- `EnsureInitialized, initialize, resetChip, identify, waitNvmReady, checkResetFlags, configure` in [`src/bmp581.ipp`](../../src/bmp581.ipp)
- `kChipIdValue, kIntStatusAfterReset, kCmdSoftReset` in [`inc/bmp581_registers.hpp`](../../inc/bmp581_registers.hpp)
- `kPowerUpUs, kSoftResetUs` in [`inc/bmp581_types.hpp`](../../inc/bmp581_types.hpp)
