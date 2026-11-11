# Quickstart

Run the driver on your PC first, with no hardware; then take a measurement on your MCU: wire the chip, write a
small port class, call two driver functions.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From Bosch Sensortec BST-BMP581-DS004-13 1.13:

    - Forced mode: 4.3.3 FORCED mode, [p. 16](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=16)
    - Pins and connection diagrams: 6.1 Pin Out; 6.2 Connection Diagrams; Table 28, [p. 46](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=46)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

## Try it on your PC

[`examples/host/`](../examples/host/README.md) runs the real driver against a simulated BMP581, through a host
port. From the repository root, with CMake and a C++20 compiler:

```bash
cmake -S examples/host -B build-example
cmake --build build-example
./build-example/bmp581_host_example
```

```text
BMP581 driver 0.1.0 on the host
bmp581: initialised
101325.000 Pa  21.5000 C
101325.000 Pa  21.5000 C
101325.000 Pa  21.5000 C
101325.000 Pa  21.5000 C
bmp581: deinitialised
```

Two forced measurements, then two in normal mode with data ready. The same calls run on your MCU with its port.

## 1. Wire the chip

The I2C connection of the datasheet (section 6.2.3): CSB to VDDIO selects I2C at power-up, SDO sets the address.

| BMP581 pin | Connect to | Notes |
| --- | --- | --- |
| VDDIO (1) | Your MCU's I/O supply | 1.08 V to 3.6 V, 100 nF to GND |
| VDD (10) | 1.71 V to 3.6 V | 100 nF to GND; any order with VDDIO |
| VSS (3, 8, 9) | GND | |
| SCK (2) | I2C SCL | Pull-up to VDDIO |
| SDI (4) | I2C SDA | Pull-up to VDDIO |
| SDO (5) | VSS | Address `0x46` (`i2c::kI2cAddress`); VDDIO gives `0x47`. Never floating |
| CSB (6) | VDDIO | Tied high from power-up: I2C mode |
| INT (7) | GND, or an MCU input | Leave INT_CONFIG.int_en = 0 (the default) when tied to GND |

For SPI and the full pin table see [Hardware](hardware.md).

## 2. Write the port

The driver calls your class for the bus and the waits. This one forwards to your SDK; `platform_*` stand for its
calls (STM32 HAL, ESP-IDF, Zephyr, Linux i2c-dev):

```cpp
--8<-- "tests/docs/my_port.hpp:port"
```

The port for SPI and the optional hooks: [Porting](porting.md).

## 3. Take a measurement

```cpp
--8<-- "tests/docs/quickstart.cpp:quickstart"
```

`EnsureInitialized()` soft-resets the chip, checks CHIP_ID, NVM readiness and the reset flag, and writes the
configuration in standby, reading it back. `MeasureOnce()` runs one forced measurement and returns the values the
chip compensated; nothing that is not a measurement is ever returned as one. Every call returns `Result<T>`: test
it before using the value. When a call fails, [Troubleshooting](troubleshooting.md) maps each `Error` to its likely
cause.

## Next

- [API guide](api-guide.md): normal mode with data ready, the FIFO, resets and errors.
- [Configuration](configuration.md): oversampling, data rates, the IIR filter and the presets.
- [Hardware](hardware.md): pins, SPI wiring, timing and power.
