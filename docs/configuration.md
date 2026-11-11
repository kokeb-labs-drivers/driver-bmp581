# Configuration

Everything `EnsureInitialized()` and `ApplyConfig()` write to the chip is one `bmp581::Config`.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From Bosch Sensortec BST-BMP581-DS004-13 1.13:

    - Power modes, deep standby and mode transitions: 4.3 Power modes; 4.3.1 STANDBY mode; 4.3.2 DEEP STANDBY; 4.3.7 Mode transitions, [p. 16](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=16)
    - Normal mode and the ODR/OSR check: 4.3.4 NORMAL mode; Table 7 maximum nominal ODR setting per OSR settings, [p. 17](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=17)
    - Pressure enable and oversampling: 4.4.1 Pressure and temperature measurement enable; 4.4.2 Oversampling ratio; Table 9, [p. 18](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=18)
    - IIR filter: 4.4.4 IIR filter; Table 10 IIR filter settings and bandwidth, [p. 19](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=19)
    - FIFO: 4.6 FIFO; 4.6.1 FIFO Configuration; 4.6.3 FIFO data readout; Tables 11 to 14, [p. 22](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=22)
    - Interrupt sources, out of range and power-on reset: 4.7 Interrupts; 4.7.2.3 Out-of-range interrupt; 4.7.2.4 Power-on reset interrupt, [p. 25](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=25)
    - INT pin: 4.7.3 Interrupt pin, [p. 27](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=27)
    - INT pad drive: 7.5 Register (0x14) Interrupt configuration register, [p. 52](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=52)
    - OSR and ODR configuration: 7.33 Register (0x36) OSR configuration; 7.34 Register (0x37) ODR configuration; 7.35 OSR_EFF, [p. 64](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=64)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

## Defaults and presets

`Config{}` holds the datasheet's reset values (section 7), with one exception: pressure is always measured
(OSR_CONFIG.press_en = 1), which also makes OSR_CONFIG the driver's reset sentinel. With `Config{}` the chip waits
in deep standby for `MeasureOnce()`.

| Preset | Mode | What it is for |
| --- | --- | --- |
| `Config{}` | Standby | Forced measurements, x1 oversampling, deep standby: the lowest current |
| `ForcedPreset()` | Standby | Forced measurements at standard resolution: pressure x4 (table 9) |
| `LowPowerPreset()` | Normal, 1 Hz | Deep standby between measurements (section 4.3.5), data ready on |
| `HighResolutionPreset()` | Normal, 25 Hz | Pressure x16, IIR coefficient 3 on the data registers, data ready on |
| `FifoPreset()` | Normal, 10 Hz | Pressure x8, pressure-and-temperature frames, threshold at 8 frames |

## Fields

| Field | Register | Range | Reset value |
| --- | --- | --- | --- |
| `mode` | ODR_CONFIG.pwr_mode | `Standby`, `Normal`, `Continuous` (`Forced` is `MeasureOnce()`) | `Standby` |
| `pressure_oversampling` | OSR_CONFIG.osr_p | x1 to x128 | x1 |
| `temperature_oversampling` | OSR_CONFIG.osr_t | x1 to x128 | x1 |
| `odr` | ODR_CONFIG.odr | 0.125 Hz to 240 Hz, 32 rates (section 7.34) | 1 Hz |
| `deep_standby` | ODR_CONFIG.deep_dis = 0 | | true |
| `pressure_iir`, `temperature_iir` | DSP_IIR | `Bypass`, coefficients 1 to 127 (table 10) | `Bypass` |
| `iir_to_data_registers`, `iir_to_fifo`, `iir_to_out_of_range` | DSP_CONFIG | | false |
| `iir_flush_in_forced` | DSP_CONFIG.iir_flush_forced_en | | false |
| `fifo_frame` | FIFO_SEL.fifo_frame_sel | off, T, P, P and T | off |
| `fifo_mode` | FIFO_CONFIG.fifo_mode | streaming, stop on full | streaming |
| `fifo_threshold` | FIFO_CONFIG.fifo_threshold | 0 (off) to 31; 15 with P and T frames | 0 |
| `fifo_decimation` | FIFO_SEL.fifo_dec_sel | 0 to 7: keep every 2^n-th frame | 0 |
| `int_pin_enable`, `int_mode`, `int_polarity`, `int_drive`, `int_pad_drive` | INT_CONFIG | pad drive 0 to 15, 8 and up with open drain only (section 7.5) | off, latched, active low, open drain, 3 |
| `source_data_ready`, `source_fifo_full`, `source_fifo_threshold`, `source_out_of_range` | INT_SOURCE | | all off |
| `oor_reference_pa`, `oor_window_pa`, `oor_count` | OOR_THR_P, OOR_RANGE, OOR_CONFIG | 0 to 131071 Pa, ±0 to 255 Pa, 1 to 15 samples | 0, 0, 1 |
| `startup_timeout_ms` | (driver) | 1 to 10000 | 10 |
| `data_ready_timeout_ms` | (driver) | 1 to 10000 | 2000 |

The INT source bits also decide what INT_STATUS reports: the chip shows a flag only for an enabled source
(section 4.7.1), so `WaitDataReady()` needs `source_data_ready`.

## Your own configuration

```cpp
--8<-- "tests/docs/configuration.cpp:custom"
```

`Validate()` checks every field without the chip: enumerations, the ranges above, and in normal mode the data rate
against table 7 (x8 pressure and x1 temperature allow 140 Hz; x128 and x128 only 5 Hz). `EnsureInitialized()` and
`ApplyConfig()` call it first and send nothing when it fails.

A configuration known at build time can be proven there:

```cpp
--8<-- "tests/docs/configuration.cpp:checked"
```

## How a configuration is applied

`ApplyConfig()` on a running driver: the reset check; standby when the chip measures (ODR_CONFIG, t_standby, a read
that confirms it), because the FIFO, IIR and DSP registers take writes in standby only (section 4.3.8); INT_SOURCE =
0 and an INT_STATUS read, the order section 4.7.3.2 asks for when the INT mode changes; the twelve registers one
byte each; a full read-back; a last INT_STATUS read; then the configured mode, read back. Any difference is
`WriteMismatch`, and the configuration stays in doubt until a later `ApplyConfig()` succeeds. Changing OSR, ODR,
the mode or the FIFO frame flushes the FIFO (section 4.6.4).

## Kconfig

Under ESP-IDF, `idf.py menuconfig` offers the oversampling, the data-ready source and the timeouts.
`bmp581::KconfigDefaults()` (in `bmp581_kconfig.hpp`) builds a `Config` from them; outside ESP-IDF it equals
`Config{}`.

```cpp
--8<-- "tests/docs/configuration.cpp:kconfig"
```
