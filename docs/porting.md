# Porting

Run the driver on a new MCU by writing one class: the port, for I2C or for SPI.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From Bosch Sensortec BST-BMP581-DS004-13 1.13:

    - Interface selection (I2C, SPI): 5.1 Protocol Selection; Table 16, [p. 31](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=31)
    - SPI timing: 5.2.1 Interface timing; Table 18 SPI timings, [p. 32](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=32)
    - SPI protocol: 5.5 SPI Protocol; 5.5.2 SPI Write Operation; 5.5.3 SPI read operation; 5.5.4 SPI hybrid bursts, [p. 36](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=36)
    - I2C protocol and addresses: 5.6 I2C protocol; 5.6.1 I2C write operation; 5.6.2 I2C read operation, [p. 38](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=38)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

The driver never calls your SDK. It calls hooks on a class you derive from `bmp581::I2cInterface<Self>` (in
[`inc/bmp581_i2c_interface.hpp`](../inc/bmp581_i2c_interface.hpp)) or `bmp581::SpiInterface<Self>` (in
[`inc/bmp581_spi_interface.hpp`](../inc/bmp581_spi_interface.hpp)). The bases use CRTP and declare no hook
themselves; the driver template is constrained by the `bmp581::BusPort` concept, which accepts a class that is
exactly one of `I2cPort` and `SpiPort`, so calls are resolved at compile time (no virtual functions, nothing to
allocate) and a missing or misdeclared hook is a compile error at the point of use.

## What you implement

| Method | Bus | Required | Contract |
| --- | --- | --- | --- |
| `bool EnsureInitialized() noexcept` | both | yes | Bring the bus up on the first call (SPI: CSB high before the first frame); return `true` at once afterwards. `false` makes the driver's call return `Bus` |
| `bool Write(uint8_t addr, const uint8_t* data, std::size_t len) noexcept` | I2C | yes | START, `addr` + W, `len` bytes, STOP. `false` on a NACK or any bus error |
| `bool WriteRead(uint8_t addr, const uint8_t* tx, std::size_t tx_len, uint8_t* rx, std::size_t rx_len) noexcept` | I2C | yes | START, `addr` + W, `tx`, repeated START (or STOP and START), `addr` + R, `rx_len` bytes, STOP |
| `bool Transfer(const uint8_t* tx, uint8_t* rx, std::size_t len) noexcept` | SPI | yes | One frame: CSB low, `len` bytes out and in, CSB high; `rx` is `nullptr` on a write |
| `void DelayUs(uint32_t us) noexcept` | both | yes | Wait at least `us` microseconds |
| `bool GetLastFailure(I2cFailure& failure) noexcept` | I2C | no | After a failed transfer: an address NACK, a data NACK or other; `false` when the port cannot tell |
| `void Log(LogLevel, const char* message) noexcept` | both | no | Print one line; `message` is plain text, never a format string |

`addr` is the 7-bit address the driver was constructed with; shift it yourself if your SDK wants the 8-bit form.

## The I2C port

```cpp title="my_port.hpp"
--8<-- "tests/docs/my_port.hpp:port"
```

- **One call, one transaction.** The register address is the first byte of `Write()` and the `tx` of
  `WriteRead()`. The driver writes one register per `Write()` and reads up to 96 bytes per `WriteRead()`.
- **Raw bytes only.** Do not retry; the driver owns the framing.
- **Return `false` for a NACK or a bus error.** The driver turns it into `Bus` (`NoDevice` for an address NACK,
  with `GetLastFailure()`). The soft reset write is NACKed by the chip on purpose (section 7.36): the driver
  expects that `false`, and only an address NACK fails it. Your SDK may log that NACK.
- **Bounded time-outs.** A stuck bus fails the call; it does not block forever.

## The SPI port

```cpp title="my_port.hpp"
--8<-- "tests/docs/my_port.hpp:spi-port"
```

- **One frame per call**, CSB low from the first byte to the last; SCK may pause inside a frame.
- **Mode 0 or 3**, most significant bit first, at most 12 MHz (7 MHz below VDDIO 1.62 V).
- **CSB high between frames** for at least 80 ns, and high before the first frame: the chip selects SPI on the
  first CSB low phase with 16 or more SCK edges (section 5.1), and the driver sends that frame itself.
- **Four-wire only.** The driver never sets DRIVE_CONFIG.spi3_en, so SDO must reach your MISO.
- There is no ACK on SPI: an absent chip reads as a stuck line (`InvalidReply`, `DeviceIdMismatch`), never as
  data.

```cpp
--8<-- "tests/docs/ports.cpp:spi-driver"
```

## Bus requirements

The driver states what the BMP581 needs from each bus as `constexpr` values (handbook KB-CPP-123). Check your
port's settings against them with `static_assert`, as the example ports do:

| Constant | Value | Meaning |
| --- | --- | --- |
| `i2c::kI2cAddress` | `0x46` | 7-bit address with SDO at VSS (section 5.6) |
| `i2c::kI2cAddressSdoHigh` | `0x47` | 7-bit address with SDO at VDDIO |
| `i2c::kMinClockHz`, `i2c::kMaxClockHz` | `100'000`, `1'000'000` | SCL range (table 4); 1 MHz needs C_bus below 100 pF |
| `i2c::kRepeatedStart` | `false` | The chip keeps the register address across a STOP (figures 15, 16) |
| `spi::kSpiMode` | `0` | Mode 0; mode 3 works too (section 5.5) |
| `spi::kMsbFirst` | `true` | Most significant bit first |
| `spi::kMaxClockHz` | `12'000'000` | VDDIO at or above 1.62 V, C_bus up to 80 pF (table 4) |
| `spi::kMaxClockLowVoltageHz` | `7'000'000` | VDDIO below 1.62 V, C_bus up to 40 pF |
| `spi::kMinCsHighNs` | `80` | Idle time after a write or read access (table 18) |
| `spi::kCsSetupNs`, `spi::kCsHoldNs` | `40`, `40` | CSB lag time (table 18); the setup time is assumed the same, **not verified on hardware** |
| `spi::kContinuousWord` | `false` | SCK may pause inside a frame |

The driver never probes with an empty or zero-length transfer.

## Control pins

The BMP581 has no reset, enable or write-protect pin. On SPI the port's chip select is the only control line: it
idles high, and the port drives it high before it enables the output and before the first frame (handbook
KB-HW-018); a CSB low pulse on an I2C board would switch the chip to SPI until the next power cycle. On I2C, CSB is
tied to VDDIO on the board. INT is an output of the chip, read by the application if it wants, never by the
driver.

## Optional hooks

The driver finds the optional hooks at compile time with named `Declares*`/`Has*` concepts in `bmp581::detail`. A
missing hook falls back in a documented, safe way or fails to compile; it never skips a step silently.

| Hook | Used for | Without it |
| --- | --- | --- |
| `DelayUs()` | t_powup, t_soft_res, t_standby, the conversion wait, the polls | Compile error at the first call that waits |
| `GetLastFailure()` | I2C: an address NACK becomes `NoDevice` | Every failed transfer is `Bus` |
| `Log()` | One line per lifecycle event or failure | Nothing is logged |

An I2C port with every hook:

```cpp
--8<-- "tests/docs/ports.cpp:full-port"
```

A `GetLastFailure()` that takes its argument by value, or a `DelayUs(uint16_t)`, is a compile error: the
[compile-fail tests](testing.md) check both.

## Reference implementations

Every port lives with its example, in `examples/<platform>/`; the driver has none. [Platforms](platforms.md) lists
them and how to add one.

- [`examples/host/host_bmp581_port.hpp`](../examples/host/host_bmp581_port.hpp): any PC, against a simulated
  chip. Start from it for a host build or a test bench; it is also the smallest complete port.
- [`examples/esp32/main/esp32_bmp581_port.hpp`](../examples/esp32/main/esp32_bmp581_port.hpp): ESP-IDF I2C with
  the log hook. Start from it for any ESP-IDF target.

## Check your port

1. Build it: the ports on this page are compiled by the host CI from [`tests/docs/`](../tests/docs/my_port.hpp),
   with the driver instantiated over them.
2. Call `EnsureInitialized()`: `Ok` proves that writes and reads work, that CHIP_ID read 0x50 and that the soft
   reset happened.
3. Call `MeasureOnce()` twice a second and breathe on the chip, or lift it a metre: the temperature rises, or the
   pressure falls by about 12 Pa.

## Threads

The driver is not thread-safe. Give each driver object one owner task, or guard every call with one mutex. When
several chips share a bus, the port must also serialise the bus. See [Contract](contract.md#threading).

## Editor setup

The clang-format and clang-tidy configuration is in `_config/`, and the repository root has no copy of it. Tools
that take a path use it directly; CI does the same:

| Tool | Setting |
| --- | --- |
| Command line | `clang-format --style=file:_config/.clang-format`, `clang-tidy --config-file=_config/.clang-tidy -p build` |
| VS Code (C/C++ extension) | `"C_Cpp.clang_format_style": "file:${workspaceFolder}/_config/.clang-format"` and `"C_Cpp.codeAnalysis.clangTidy.args": ["--config-file=${workspaceFolder}/_config/.clang-tidy"]` |
| clangd (VS Code clangd, Neovim, Emacs), CLion | These find `.clang-format` and `.clang-tidy` only by name, from the file upwards: copy both to the root once (`cp _config/.clang-format _config/.clang-tidy .`). `.gitignore` lists the copies, so they never reach a commit; copy again after `_config/` changes |

Configure with `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` (clangd: `--compile-commands-dir=build`) so the editor sees the
same flags as the build.
