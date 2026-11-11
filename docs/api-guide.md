# API guide

Common tasks with `bmp581::BMP581<Port>`, task by task. Every call returns `Result<T>`; check it before using the
value. The [API reference](api-reference.md) lists every member.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From Bosch Sensortec BST-BMP581-DS004-13 1.13:

    - Measurement range, resolution and accuracy: 2 Specification; Table 1 Pressure Performance; Table 2 Temperature Performance, [p. 8](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=8)
    - Power modes, deep standby and mode transitions: 4.3 Power modes; 4.3.1 STANDBY mode; 4.3.2 DEEP STANDBY; 4.3.7 Mode transitions, [p. 16](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=16)
    - Forced mode: 4.3.3 FORCED mode, [p. 16](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=16)
    - Data registers and data shadowing: 4.5 Data registers; 4.5.1 Data Shadowing, [p. 22](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=22)
    - FIFO: 4.6 FIFO; 4.6.1 FIFO Configuration; 4.6.3 FIFO data readout; Tables 11 to 14, [p. 22](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=22)
    - FIFO flush on configuration changes: 4.6.4 FIFO configuration changes; Table 4 t_reconf, [p. 25](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=25)
    - Interrupt sources, out of range and power-on reset: 4.7 Interrupts; 4.7.2.3 Out-of-range interrupt; 4.7.2.4 Power-on reset interrupt, [p. 25](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=25)
    - Register map: 7 Register Map, [p. 50](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=50)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

## Start the chip

```cpp
--8<-- "tests/docs/api_tasks.cpp:lifecycle"
```

The first call soft-resets the chip and configures it (about 5 ms); a later call checks for a reset with one
3-byte read. Nothing touches the chip before it: every other call returns `NotInitialized`.

## Measure on request (forced mode)

```cpp
--8<-- "tests/docs/api_tasks.cpp:forced"
```

The configured mode stays standby (deep standby with `Config{}`), and each `MeasureOnce()` runs one measurement:
the reset check, ODR_CONFIG = forced, a read that proves it started, a wait of the typical conversion time less
5 %, polls until the chip is back in standby (at most `ForcedMeasurementMaxUs()`, `Timeout` after), and the data
read. In normal or continuous mode it returns `Busy` with nothing sent.

`Measurement` holds the chip's own units. Convert with `PressurePa(m.pressure_raw)` (1/64 Pa per count) and
`TemperatureC(m.temperature_raw)` (1/65536 °C per count); both are exact.

## Stream in normal mode

```cpp
--8<-- "tests/docs/api_tasks.cpp:normal"
```

`WaitDataReady()` polls INT_STATUS every `kPollUs` until drdy, at most `Config::data_ready_timeout_ms`; it needs
`Config::source_data_ready` (the chip reports drdy only for an enabled source, section 4.7.1). `ReadMeasurement()`
reads the data registers whatever the mode; in standby it returns the last measurement. Each INT_STATUS read
clears the flags on the chip; the driver keeps them in a latch, `GetLatchedInterrupts()`, until
`ClearLatchedInterrupts()`.

`SetPowerMode()` changes the mode alone: standby, normal or continuous, always through standby and read back. In
normal mode the chip checks the data rate against the oversampling; when it disagrees (OSR_EFF.odr_is_valid = 0)
the call returns `ConfigRejected` and leaves the chip in standby. `Validate()` refuses those combinations first,
from table 7.

## Read the FIFO

```cpp
--8<-- "tests/docs/api_tasks.cpp:fifo"
```

`ReadFifo()` reads FIFO_COUNT, then whole frames in one burst from FIFO_DATA, as many as `out` holds; the rest
stay. Each `FifoSample` has its own `error`: `NoData` for the empty frame (0x7F7F7F), `OutOfRange`. A
temperature-and-pressure frame fills both values; a single-value frame says which one it has. A configuration
change flushes the FIFO (section 4.6.4). An empty FIFO runs the reset check, because a reset empties it.

## Handle a reset

```cpp
--8<-- "tests/docs/api_tasks.cpp:reset"
```

Data reads, polls and setters find a reset by themselves and return `DeviceReset`; the driver is then
uninitialised, so the next call is `EnsureInitialized()`. How the driver tells a reset from a broken line:
[Contract](contract.md#resets).

## Handle errors

```cpp
--8<-- "tests/docs/api_tasks.cpp:errors"
```

| Error | What to do |
| --- | --- |
| `NoDevice`, `Bus` | Check power, wiring and the address (I2C) or CSB (SPI); retry `EnsureInitialized()` |
| `DeviceIdMismatch` | Another part answers, or the soft reset did not happen: see [Troubleshooting](troubleshooting.md) |
| `InvalidReply` | A line is stuck, floating or disturbed: check pull-ups, SDO, CSB; the next call reads again |
| `DeviceReset` | Call `EnsureInitialized()` |
| `NoData` | No measurement since the reset: start one (`MeasureOnce()`, normal mode) |
| `OutOfRange` | The environment is outside the sensor's range, or the reading is wrong |
| `WriteMismatch`, `StateUnknown` | Call `ApplyConfig()` again; check the bus |
| `Timeout` | See [Troubleshooting](troubleshooting.md) |
| `Busy` | Switch to standby before `MeasureOnce()` |
| `Unsupported` | Enable the data-ready source or the FIFO in `Config` |
| `ConfigRejected` | Lower the data rate or the oversampling (table 7) |

`Crc` is never returned: neither bus carries a CRC.

## Diagnostics and raw access

- `ReadRegister(address)` reads one register once, with no retry (KB-CPP-129); FIFO_DATA and CMD are refused. A
  raw INT_STATUS read clears the flags and feeds the latch.
- `GetLastIntStatus()`, `GetLastStatus()`, `GetLastOsrEff()`, `GetRevision()` return what the last read saw, with
  no I/O.
- `ReadInterruptStatus()` reads INT_STATUS once and decodes it.
- `ReadMeasurement(Verify::ReadBack)` and `ReadFifo(out, Verify::ReadBack)` add the reset check, one 3-byte read.
  `ReadMeasurement(Verify::ReadBack)` and `MeasureOnce(Verify::ReadBack)` also read the data registers a second
  time (6 bytes, a third when they differ): a bit flipped inside a data byte is not detectable without it.

## Shut down

```cpp
--8<-- "tests/docs/api_tasks.cpp:deinit"
```

`Deinitialize()` always does its work, also before a start or after a reset; the driver is uninitialised
afterwards either way.
