# BMP581 ESP32 examples

ESP-IDF applications that run the BMP581 driver on an ESP32-C6, built by CI on every pull request.

## Wiring (ESP32-C6 DevKitC-1)

The I2C connection of the datasheet (section 6.2.3, figure 26):

| ESP32-C6 | BMP581 pin | Notes |
| --- | --- | --- |
| GPIO6 | SDX (4) | I2C0 SDA, 400 kHz; internal pull-ups enabled, fit 4.7 kΩ to VDDIO for 400 kHz or long wires |
| GPIO7 | SCK (2) | I2C0 SCL |
| 3V3 | CSB (6) | CSB tied to VDDIO selects I2C at power-up |
| GND | SDO (5) | Address `0x46`; to VDDIO for `0x47`. Never floating |
| 3V3 | VDDIO (1), VDD (10) | 100 nF to GND at each |
| GND | VSS (3, 8, 9) | |
| (optional) | INT (7) | Not used by this example: tie to GND or leave unconnected (section 6.2) |

## Build and flash

```bash
git submodule update --init scripts      # the ESP-IDF build scripts
. $IDF_PATH/export.sh                     # ESP-IDF v5.5
./scripts/build_app.sh list
./scripts/build_app.sh bmp581_basic_example Debug
./scripts/flash_app.sh flash_monitor bmp581_basic_example Debug
```

`idf.py menuconfig` shows "BMP581 pressure sensor" under Component config: the oversampling, the data-ready source
and the timeouts that `bmp581::KconfigDefaults()` returns.

## Expected output

```text
I (312) bmp581_example: BMP581 driver 0.1.0
bmp581: initialised
I (318) bmp581_example: initialised, ASIC revision 0x32
I (330) bmp581_example: 101325.000 Pa 21.5000 C
```

i2c_master may log one NACK during `EnsureInitialized()`: the soft reset write, which the chip does not acknowledge
over I2C (section 7.36). An `EnsureInitialized failed: DeviceIdMismatch` line means CHIP_ID did not read 0x50, or
INT_STATUS did not show the soft reset: check SDO, CSB and the pull-ups first.

## Files

| File | What it is |
| --- | --- |
| [`main/bmp581_basic_example.cpp`](main/bmp581_basic_example.cpp) | The example app |
| [`main/esp32_bmp581_port.hpp`](main/esp32_bmp581_port.hpp) | The ESP-IDF port: bus, delay, log |
| [`CMakeLists.txt`](CMakeLists.txt) | The ESP-IDF project; `EXTRA_COMPONENT_DIRS` points at the repository root, which is the component |
| [`app_config.yml`](app_config.yml) | The app matrix for the scripts, CI and the docs |
