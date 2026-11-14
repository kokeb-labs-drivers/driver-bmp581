# BMP581 Driver

<!-- generated: chip-summary -->
The **BMP581** is an absolute barometric pressure sensor that measures pressure from 30 kPa to 125 kPa and temperature,
and linearises and compensates both on the chip (1 Basic Description, [p.
2](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=2)).
<!-- /generated -->

C++20 driver for the Bosch BMP581 barometric pressure and temperature sensor on I2C and SPI.

[![CI](https://github.com/kokeb-labs-drivers/driver-bmp581/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/kokeb-labs-drivers/driver-bmp581/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-informational)](docs/installation.md#requirements)

**Documentation: [`docs/`](docs/index.md)**, with the API reference built by Doxygen ([how](docs/api-reference.md)).
The site at <https://kokeb-labs-drivers.github.io/driver-bmp581/> opens when the repository is public.

<!-- figure: chip-hero -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/diagrams/chip-hero.dark.svg">
  <img src="docs/diagrams/chip-hero.svg"
    alt="Overview of the BMP581, an absolute barometric pressure sensor that measures pressure from 30 kPa to
    125 kPa and temperature, and linearises and compensates both on the chip. A MCU talks to it over I2C
    or SPI; it drives INT line; it reads back Air pressure, Temperature. Headline figures: 30 to 125 kPa
    pressure measurement range; 1/64 Pa pressure data resolution; 1.3 µA at 1 Hz in low-power mode; 1.71
    to 3.6 V VDD; VDDIO 1.08 to 3.6 V; 12 MHz SPI clock; I2C up to 1 MHz; 2.0 x 2.0 mm LGA-10 body, 0.8
    mm high at most.">
</picture>
<!-- /figure -->

## What it does

<!-- figure: chip-capabilities -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/diagrams/chip-capabilities.dark.svg">
  <img src="docs/diagrams/chip-capabilities.svg"
    alt="Capability map of the BMP581: Pressure and temperature (supported): Reads both compensated results
    in one burst, 1/64 Pa and 1/65536 °C per count; Power modes (supported): One forced measurement on
    request, normal mode at a set rate, continuous, standby or deep standby; Oversampling and data rate
    (supported): Trades noise for current and speed, x1 to x128, with the rate checked against the
    chosen oversampling; IIR filter (supported): Low-pass filters pressure and temperature, for the data
    registers, the FIFO or the out-of-range check; FIFO (supported): Buffers up to 32 frames, streaming
    or stop-on-full, with a threshold and decimation; Interrupts (supported): Data ready, FIFO full and
    FIFO threshold on the INT pin, push-pull or open drain; Out-of-range detection (supported): Flags
    pressure that leaves a window around a reference for 1, 3, 7 or 15 samples in a row; Reset and
    identity (supported): Soft reset, chip ID and NVM checks at start, and a power-on reset found mid-
    session; NVM, I3C and 3-wire SPI (not covered): Six user NVM bytes, the I3C bus and 3-wire SPI; the
    driver never programs the NVM.">
</picture>
<!-- /figure -->

## Where it is used

<!-- generated: chip-uses -->
- GPS navigation enhancement and dead reckoning
- Indoor navigation, floor and elevator detection
- Outdoor navigation
- Sports and fitness activity tracking
- Emergency caller location
- Weather forecast
- Vertical speed indication
- Altitude control of drones and flying toys

Uses named by the datasheet (1 Basic Description, [p. 2](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=2)).
<!-- /generated -->

## Inside the chip

<!-- figure: chip-blocks -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/diagrams/chip-blocks.dark.svg">
  <img src="docs/diagrams/chip-blocks.svg"
    alt="Block diagram of the BMP581: its function blocks, how they connect and which package pins reach each
    block.">
</picture>
<!-- /figure -->

## Pins and wiring

<!-- figure: chip-pinout -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/diagrams/chip-pinout.dark.svg">
  <img src="docs/diagrams/chip-pinout.svg"
    alt="Pinout of the BMP581 in LGA-10, top view: 10 pins, each with its name, its electrical type as a
    colour and the driver hook that uses it.">
</picture>
<!-- /figure -->

<!-- figure: chip-wiring-i2c -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/diagrams/chip-wiring-i2c.dark.svg">
  <img src="docs/diagrams/chip-wiring-i2c.svg"
    alt="Wiring of the BMP581 for the I2C connection: MCU I2C SCL to SCK (WriteRead() or Transfer()); MCU I2C
    SDA to SDI (WriteRead() or Transfer()); MCU GPIO in (INT) to INT. Supplies, grounds and the support
    parts are listed under the drawing.">
</picture>
<!-- /figure -->

| MCU | BMP581 pin | Notes |
| --- | --- | --- |
| I2C SCL | SCK (2) | Pull-up to VDDIO; 4.7 kΩ suits 100 kHz and 400 kHz |
| I2C SDA | SDI (4) | Pull-up to VDDIO |
| GND | SDO (5) | Address `0x46` (`i2c::kI2cAddress`); VDDIO gives `0x47`; never floating |
| I/O supply | CSB (6), VDDIO (1) | CSB tied to VDDIO selects I2C; 100 nF at VDDIO |
| 1.8 V to 3.3 V | VDD (10) | 100 nF to GND close to the pin |
| GND | VSS (3, 8, 9) | INT (7) to GND when unused |

The figure follows the datasheet's I2C diagram, SDO to VDDIO for address `0x47` (`i2c::kI2cAddressSdoHigh`); the
table ties SDO to GND for the default `0x46`. On SPI: SCK, SDI (MOSI), SDO (MISO) and CSB to the MCU's SPI pins ([Hardware](docs/hardware.md#spi)).

<!-- generated: chip-card -->
| | | Datasheet |
| --- | --- | --- |
| **Package** | LGA-10 | 6.1, [p. 46](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=46) |
| **Pins** | 10 | 6.1, [p. 46](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=46) |
| **Supply VDD** | 1.71 V to 3.6 V | 2, [p. 10](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=10) |
| **Supply VDDIO** | 1.08 V to 3.6 V | 2, [p. 10](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=10) |

From [`docs/hardware.yml`](docs/hardware.yml); pins, circuits and the block diagram: [Hardware](docs/hardware.md).
<!-- /generated -->

## Quick start

**1. Wire it** as in [Pins and wiring](#pins-and-wiring).

**2. Write the port**: a class with `EnsureInitialized()` (bring the bus up), `Write()` and `WriteRead()` (one I2C
transaction each), or `Transfer()` (one SPI frame), and `DelayUs()`, about 20 lines on most SDKs.
[Porting](docs/porting.md) has the contract and complete examples for both buses.

**3. Take a measurement:**

<!-- generated: quickstart-code -->
```cpp
#include "bmp581.hpp"

int read_pressure(MyPort& port, float& pascals, float& celsius) {
  bmp581::BMP581<MyPort> sensor(port, bmp581::ForcedPreset()); // no I/O yet

  if (auto r = sensor.EnsureInitialized(); !r) { // 1. soft reset, CHIP_ID, NVM ready, Config
    return static_cast<int>(r.error); //    NoDevice, Bus or DeviceIdMismatch: check wiring
  }
  const auto m = sensor.MeasureOnce(); // 2. one forced measurement, then standby
  if (!m) {
    return static_cast<int>(m.error); //    every failure is an Error, never a number
  }
  pascals = bmp581::PressurePa(m.value.pressure_raw);      // 3. 1/64 Pa, exact
  celsius = bmp581::TemperatureC(m.value.temperature_raw); //    1/65536 °C, exact
  return 0;
}
```
<!-- /generated -->

`EnsureInitialized()` is the only call that starts the chip. Every other call returns `Error::NotInitialized`
until it has succeeded, and no read ever starts the chip on its own.

## Driver coverage

<!-- generated: chip-coverage -->
| Capability | What the chip does | Driver API | Registers | Status | Datasheet |
| --- | --- | --- | --- | --- | --- |
| [Pressure and temperature](docs/api-guide.md) | Reads both compensated results in one burst, 1/64 Pa and 1/65536 °C per count | `ReadMeasurement`, `PressurePa`, `TemperatureC` | `PRESS_DATA_XLSB`, `PRESS_DATA_LSB`, `PRESS_DATA_MSB`, `TEMP_DATA_XLSB`, `TEMP_DATA_LSB`, `TEMP_DATA_MSB` | Supported | 4.5, [p. 22](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=22) |
| [Power modes](docs/api-guide.md) | One forced measurement on request, normal mode at a set rate, continuous, standby or deep standby | `MeasureOnce`, `SetPowerMode`, `PowerMode` | `ODR_CONFIG` | Supported | 4.3, [p. 16](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=16) |
| [Oversampling and data rate](docs/configuration.md) | Trades noise for current and speed, x1 to x128, with the rate checked against the chosen oversampling | `Oversampling`, `Validate`, `MaxOdrMilliHz`, `GetLastOsrEff` | `OSR_CONFIG`, `ODR_CONFIG`, `OSR_EFF` | Supported | 4.4.2, [p. 19](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=19) |
| [IIR filter](docs/configuration.md) | Low-pass filters pressure and temperature, for the data registers, the FIFO or the out-of-range check | `IirFilter`, `ApplyConfig` | `DSP_CONFIG`, `DSP_IIR` | Supported | 4.4.4, [p. 19](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=19) |
| [FIFO](docs/api-guide.md) | Buffers up to 32 frames, streaming or stop-on-full, with a threshold and decimation | `ReadFifo`, `ReadFifoCount`, `FifoFrame` | `FIFO_CONFIG`, `FIFO_COUNT`, `FIFO_SEL`, `FIFO_DATA` | Supported | 4.6, [p. 22](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=22) |
| [Interrupts](docs/configuration.md) | Data ready, FIFO full and FIFO threshold on the INT pin, push-pull or open drain | `ReadInterruptStatus`, `WaitDataReady`, `GetLatchedInterrupts` | `INT_CONFIG`, `INT_SOURCE`, `INT_STATUS` | Supported | 4.7, [p. 25](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=25) |
| [Out-of-range detection](docs/configuration.md) | Flags pressure that leaves a window around a reference for 1, 3, 7 or 15 samples in a row | `OorCount`, `kOorReferenceMaxPa`, `ReadInterruptStatus` | `OOR_THR_P_LSB`, `OOR_THR_P_MSB`, `OOR_RANGE`, `OOR_CONFIG` | Supported | 4.7.2.3, [p. 26](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=26) |
| [Reset and identity](docs/contract.md) | Soft reset, chip ID and NVM checks at start, and a power-on reset found mid-session | `EnsureInitialized`, `CheckForReset`, `GetRevision` | `CMD`, `CHIP_ID`, `REV_ID`, `STATUS` | Supported | 4.3.9, [p. 18](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=18) |
| [NVM, I3C and 3-wire SPI](docs/hardware.md) | Six user NVM bytes, the I3C bus and 3-wire SPI; the driver never programs the NVM | - | `NVM_ADDR`, `DRIVE_CONFIG` | Not covered | 4.8, [p. 29](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=29) |

**Supported**: the driver covers it and a host test checks it; **Not covered**: the chip has it and the driver leaves it
alone.

From [`docs/hardware.yml`](docs/hardware.yml); `kokeb-docs hardware --check` confirms that every API name is in
`inc/bmp581.hpp`, `inc/bmp581_types.hpp` and every register in `inc/bmp581_registers.hpp`.
<!-- /generated -->

This driver starts the chip from a soft reset with the datasheet's checks, measures on request or continuously, reads the
FIFO, and never returns a stuck line, a reset value or an out-of-range reading as a measurement. It finds a power-on
reset mid-session and never writes its settings over a reset chip. It runs on any MCU, on I2C or SPI: you give it a
small class that moves bytes on your bus, and it does the rest.

| | |
| --- | --- |
| **Bus** | I2C up to 1 MHz at `0x46` or `0x47` (SDO), or four-wire SPI up to 12 MHz, mode 0 or 3 |
| **Driver** | Header-only C++20; no heap, exceptions, RTTI or virtual calls; every call returns `Result<T>` |
| **Runs on** | Any MCU with I2C or SPI and a C++20 compiler; the host example runs on any PC ([Platforms](docs/platforms.md)) |
| **Status** | 0.1.0, not yet verified on hardware: host-tested against a register model of the chip |

- Brings the chip up with a soft reset and the datasheet's post-power-up checks (CHIP_ID, NVM ready, the reset
  flag); nothing else touches the chip before that succeeds.
- Writes the configuration in standby, one register per transfer, and reads all of it back.
- Measures on request (forced mode, a bounded wait) or streams in normal or continuous mode with data ready; reads
  the FIFO with a validity per frame.
- Reads each measurement in one burst and refuses a stuck or floating line, the reset value and anything outside
  the measurement range; converts exactly to pascals and degrees Celsius.
- Finds a power-on reset by INT_STATUS and by OSR_CONFIG, confirms it with a second read, and never writes over it.
- Returns `Result<T>` with an `Error` from every call: no exceptions, no heap, no RTTI, no virtual calls.

## Documentation

| Page | Read it when you want to |
| --- | --- |
| [Quickstart](docs/quickstart.md) | Go from a bare chip to a measurement on your MCU |
| [Hardware](docs/hardware.md) | Wire it, check the bus settings, the timing and the power |
| [Porting](docs/porting.md) | Write the port class for your MCU's SDK |
| [API guide](docs/api-guide.md) | Use each call: start, measure, stream, read the FIFO, handle resets and errors |
| [Configuration](docs/configuration.md) | Choose every setting, or start from a preset |
| [Troubleshooting](docs/troubleshooting.md) | Find the cause of an error or a wrong reading |
| [Contract](docs/contract.md) | Know exactly what the driver promises, needs and returns |
| [Datasheet map](docs/datasheet.md) | Find the datasheet section behind each part of the driver |
| [API reference](docs/api-reference.md) | Look up a class or member (Doxygen; built by CI and locally) |

## Datasheet

<!-- generated: datasheet-card -->
| Document | Revision | Get it |
| --- | --- | --- |
| Datasheet: BMP581 Barometric Pressure Sensor (Bosch Sensortec BST-BMP581-DS004-13) | 1.13, 2025-04 | [PDF](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf) · [vendor page](https://www.bosch-sensortec.com/products/environmental-sensors/pressure-sensors/bmp581/) |

Written against these revisions; the SHA-256 of each file is in [`docs/datasheets.yml`](docs/datasheets.yml). Where each
part of the driver is in the datasheet: [Datasheet map](docs/datasheet.md).
<!-- /generated -->

Every docs page lists the datasheet sections it relies on, and code comments cite the section they implement.

## Try it on your PC

No hardware needed: [`examples/host`](examples/host/README.md) runs the driver against a simulated chip.
You need git, CMake 3.16+ and a C++20 compiler (GCC 10+, Clang 13+ or MSVC 2022).

1. Get the source:

   ```bash
   git clone https://github.com/kokeb-labs-drivers/driver-bmp581.git && cd driver-bmp581
   ```

2. Build and run the example:

   ```bash
   cmake -S examples/host -B build-example
   cmake --build build-example
   ./build-example/bmp581_host_example
   ```

3. You should see the driver start the simulated chip, measure twice in forced mode and twice in normal mode:

   ```text
   BMP581 driver 0.1.0 on the host
   bmp581: initialised
   101325.000 Pa  21.5000 C
   101325.000 Pa  21.5000 C
   101325.000 Pa  21.5000 C
   101325.000 Pa  21.5000 C
   bmp581: deinitialised
   ```

To run the whole test suite instead: `cmake -S . -B build -DBMP581_BUILD_TESTS=ON && cmake --build build && ctest
--test-dir build`.

## Add it to your project

Pick the row that matches your build; each is a few steps, and [Installation](docs/installation.md) has the
details.

| Your build | Steps below |
| --- | --- |
| Plain CMake: STM32 (CubeMX), RP2040 (Pico SDK), nRF Connect, a host app | [A](#a-cmake-any-mcu) |
| ESP-IDF | [B](#b-esp-idf) |
| An SDK or OS image that installs libraries | [C](#c-installed-package) |

### A. CMake, any MCU

1. Add the driver as a submodule, pinned to the version you chose:

   ```bash
   git submodule add https://github.com/kokeb-labs-drivers/driver-bmp581.git external/driver-bmp581
   ```

2. In your `CMakeLists.txt`, after `project()`:

   ```cmake
   add_subdirectory(external/driver-bmp581)
   target_link_libraries(my_app PRIVATE bmp581::bmp581)   # brings the headers and C++20
   ```

3. In your code, `#include "bmp581.hpp"`, write the port ([Porting](docs/porting.md)) and build. A first build
   that compiles the quickstart above is the check that the driver is wired in.

### B. ESP-IDF

1. Add the driver under your project's `components/`; ESP-IDF finds it there and names it after the folder:

   ```bash
   git submodule add https://github.com/kokeb-labs-drivers/driver-bmp581.git components/bmp581
   ```

2. In `main/CMakeLists.txt`, list it: `idf_component_register(SRCS "main.cpp" REQUIRES bmp581)`.
3. `idf.py menuconfig` now has a **BMP581 pressure sensor** menu (oversampling, data ready, timeouts),
   and `idf.py build` builds the driver with your app. [`examples/esp32`](examples/esp32) is a complete
   project with a port for the board.

### C. Installed package

1. Install it once into a prefix:

   ```bash
   cmake -S external/driver-bmp581 -B build-bmp581 && cmake --install build-bmp581 --prefix /opt/sdk
   ```

2. In your project, with `CMAKE_PREFIX_PATH=/opt/sdk`:

   ```cmake
   find_package(bmp581 0.1 REQUIRED)
   target_link_libraries(my_app PRIVATE bmp581::bmp581)
   ```

The target is `bmp581::bmp581` every way, so switching later changes nothing else.

## Tests and quality

```bash
cmake -S . -B build -DBMP581_BUILD_TESTS=ON && cmake --build build && ctest --test-dir build
```

The host tests run the driver on I2C and SPI against a register model of the chip that measures and resets on its
own, fail every bus transfer in turn, and produce each error the driver can return. A fuzzer fails, corrupts and
replaces what the chip sends back, resets the chip at any point, and checks after every call that no broken line
is returned as data and no reset is missed. CI also builds for Cortex-M and gates coverage, complexity, sanitizers,
fuzzing, clang-tidy, cppcheck and the handbook rules (`.kokeb/handbook.yml`). See [Testing and CI](docs/testing.md),
which lists every gate and its limit.

## License

MIT. See [LICENSE](LICENSE).
