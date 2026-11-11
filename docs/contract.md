# Contract

What the BMP581 driver promises, what it needs, and the rules a caller follows.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From Bosch Sensortec BST-BMP581-DS004-13 1.13:

    - Supplies, timing and conversion times: 2 Specification; Table 4 Electrical characteristics, [p. 10](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=10)
    - Power modes, deep standby and mode transitions: 4.3 Power modes; 4.3.1 STANDBY mode; 4.3.2 DEEP STANDBY; 4.3.7 Mode transitions, [p. 16](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=16)
    - Register writes only in standby: 4.3.8 Mode-depending register write restrictions, [p. 18](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=18)
    - Soft reset: 4.3.10 Soft reset; 7.36 Register (0x7E) Command Register, [p. 18](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=18)
    - Pressure enable and oversampling: 4.4.1 Pressure and temperature measurement enable; 4.4.2 Oversampling ratio; Table 9, [p. 18](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=18)
    - FIFO flush on configuration changes: 4.6.4 FIFO configuration changes; Table 4 t_reconf, [p. 25](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=25)
    - Interrupt sources, out of range and power-on reset: 4.7 Interrupts; 4.7.2.3 Out-of-range interrupt; 4.7.2.4 Power-on reset interrupt, [p. 25](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=25)
    - NVM data registers (the reset marker): 4.8.1 NVM User Range; 7.25 Register (0x2C) NVM data (LSB); 7.26 Register (0x2D) NVM data (MSB), [p. 29](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=29)
    - Interrupt status (clear-on-read): 7.21 Register (0x27) Interrupt status register (clear-on-read), [p. 58](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=58)
    - OSR and ODR configuration: 7.33 Register (0x36) OSR configuration; 7.34 Register (0x37) ODR configuration; 7.35 OSR_EFF, [p. 64](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf#page=64)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

The driver inherits the [Kokeb Labs driver contract](https://github.com/kokeb-labs/.github/blob/main/standards/driver.md);
this page states only what is specific to it. A change to anything here is a breaking change, listed under
**Breaking** in the [changelog](changelog.md).

## Promises

- The constructor is `noexcept`, stores the configuration, does no I/O and cannot fail.
- `EnsureInitialized()` is the only way to start the driver. Its first call checks the stored configuration and
  the I2C address (`InvalidArgument`, nothing sent), brings the bus up, waits t_powup, soft-resets the chip,
  checks CHIP_ID, NVM readiness and the reset flag, and only then writes the configuration, in standby, and reads
  it back. On any failure the driver stays uninitialised and the next call starts again from the beginning. Once
  started, a later call runs the reset check and returns `Ok`, or starts again when the chip reset.
- Every call that touches the chip returns `NotInitialized` before `EnsureInitialized()` succeeds, except
  `Deinitialize()`. No call starts the chip as a side effect. `ApplyConfig()` before the start only stores the
  configuration.
- `InvalidArgument` means nothing was sent.
- Every port failure propagates (`Bus`, or `NoDevice` for an I2C address NACK the port reports); a failed value is
  `T{}`.
- Every wait is bounded by a datasheet maximum or a validated `Config` timeout: `kTimeoutMaxMs` at most.
- `Config{}` equals the datasheet reset values, except that pressure is always measured (`OSR_CONFIG.press_en`).

## Values

- A measurement comes from one 12-byte read of TEMP_DATA to STATUS, so temperature and pressure come from one
  conversion (section 4.5.1).
- No reply a broken line can produce is ever a value: a frame whose reserved registers (0x23 to 0x26) are not 0x00
  or whose reserved bits are set (all 0xFF), or a frame of all 0x00, is `InvalidReply`.
- The reset value 0x7F7F7F is `NoData`, never a reading; a value outside 30 kPa to 125 kPa or -40 °C to 85 °C is
  `OutOfRange`, never clamped.
- A FIFO read reports validity per frame (`FifoSample::error`); a FIFO burst of all 0x00 or all 0xFF is
  `InvalidReply` for the whole call.
- The conversions are exact: pressure_raw / 64 Pa and temperature_raw / 65536 °C (section 4.5).

## Resets

A power-on reset (a supply dip, a brown-out) or a soft reset loads every register with its reset value and sets
INT_STATUS.por (sections 4.7.2.4, 7.21). The chip then sits in deep standby (section 4.3): it measures nothing,
and a driver that did not notice would keep reading the old data or write its cached settings over the reset chip.

- **The sentinel.** The driver always writes OSR_CONFIG with press_en = 1, and only a reset clears it, so
  OSR_CONFIG.press_en = 0 is the reset. No write of the driver erases it.
- **The marker.** 0x3C and 0xC3 in NVM_DATA_LSB and NVM_DATA_MSB (reset value 0x00, inert without an NVM
  command in CMD, sections 4.8.1, 7.25, 7.26). NVM_DATA is the NVM read port, and the NVM is downloaded at the
  start of every measurement from deep standby (sections 4.8.1.1, 7.22); the datasheet does not say whether that
  reaches NVM_DATA, so a download may leave any value there, 0x00 0x00 (an unprogrammed word) included. The
  driver therefore counts the marker only while it is fresh: written or verified in standby, with no
  measurement from deep standby since.
  - Every configuration sequence, after the call's reset check and with the chip in standby with deep standby
    off (no download can follow until the sequence ends), reads NVM_DATA. A fresh marker that reads 0x00 0x00
    twice is a reset (`DeviceReset`, nothing written). Otherwise anything but the marker is overwritten at once,
    and the sentinel is read again: the sequence has not written OSR_CONFIG yet, so a reset between the call's
    check and the new marker still shows.
  - The sequence itself does not write NVM_DATA, so 0x00 0x00 at its read-back is a reset during it, even when
    the sequence's own OSR_CONFIG write covered the sentinel.
  - The decision on a por the sentinel contradicts uses the marker only while it is fresh; otherwise the
    configuration alone decides.
  - Any value other than the marker or 0x00 0x00 is the marker lost, never a reset.
- **The evidence.** Every INT_STATUS read (data reads, polls, `ReadInterruptStatus()`, a raw read) is checked for
  por; the data registers at their reset value are evidence too. Each runs the reset check: OSR_CONFIG, ODR_CONFIG
  and OSR_EFF in one read.
- **Confirmed before acting.** Reset evidence is read a second time: press_en = 0, a reserved bit set in
  OSR_CONFIG or OSR_EFF (a reply that cannot come from the chip; no constant byte passes these checks, so neither
  a stuck line nor the SPI frame that only selects SPI again does), or, with the configuration known,
  OSR_CONFIG or ODR_CONFIG differing from what the driver wrote and read back. A second read that shows it again
  is the reset: the driver is then uninitialised and returns `DeviceReset`; call `EnsureInitialized()`. One
  corrupt read costs one more read.
- **Never written over.** `ApplyConfig()`, `SetPowerMode()` to a measuring mode and `MeasureOnce()` read
  INT_STATUS and then OSR_CONFIG and ODR_CONFIG before their first write, and a reset found sends nothing.
- **A por the sentinel contradicts.** When INT_STATUS shows por but OSR_CONFIG does not, either the por read was
  corrupt or a reset was followed by an OSR_CONFIG write (a sequence whose reads were disturbed). With the
  configuration known, the whole configuration and the marker decide: a chip that no longer holds them reset; one
  that holds them gives `InvalidReply`, and the next call checks again. When those reads fail, the decision moves
  to the next call, which writes nothing before it.
- **Doubt is never dropped.** A check that is due but cannot be read stays pending, and no call returns `Ok` until
  it has passed: a call that would succeed fails with the check's error, a call that failed keeps its own (a
  confirmed reset outranks it).
- **In a configuration sequence** the sequence itself rewrites OSR_CONFIG, so a por read at its end cannot be
  confirmed: it is taken as the reset, the safe direction for a sensor. A sequence that fails after its first
  configuration write leaves the driver uninitialised, whatever the error: a reset during it could hide behind its
  own writes and a lost INT_STATUS read, so the next `EnsureInitialized()` starts again from the soft reset.
- **An empty FIFO** (with the FIFO on), **an empty FIFO frame** and **a FIFO frame out of range** run the reset
  check too: a reset empties and disables the FIFO.
- **On SPI** the first frame after a reset only selects SPI again, and its bytes are whatever MISO shows (section
  5.1): an INT_STATUS, FIFO_COUNT, FIFO_DATA or raw read cannot tell such a frame from data. On SPI,
  `ReadInterruptStatus()`, `WaitDataReady()` (when it sees drdy), `ReadFifoCount()`, `ReadFifo()` and
  `ReadRegister()` therefore run the reset check before they return what they read: one more 3-byte read. The data
  read needs none: its reserved registers refuse every constant frame. A pull-up on MISO (see
  [Hardware](hardware.md)) makes the frame read 0xFF.

A line stuck low reads OSR_CONFIG = 0x00 twice and is reported as a reset; `EnsureInitialized()` then fails with
`DeviceIdMismatch`. On I2C, calls that only read status (`ReadInterruptStatus()`, `ReadFifoCount()`,
`ReadRegister()`) report what they read and run the reset check only on evidence; where a supply dip is possible
and the application only reads status, call `CheckForReset()`.

**A bit flipped inside a data byte.** The bus carries no CRC: a single bit flipped by noise inside a data byte (a
temperature LSB, a FIFO sample) gives a plausible value that no reserved bit or range check can refuse, and it is
not detectable without a second read. `ReadMeasurement(Verify::ReadBack)` and `MeasureOnce(Verify::ReadBack)` read
TEMP_DATA and PRESS_DATA again (6 bytes): equal is `Ok`; otherwise a third read decides, equal to either earlier
read (a new measurement in normal or continuous mode, or one disturbed read), and three different values, or two
reads of a stuck line, are `InvalidReply`. A FIFO read cannot be repeated: the frames leave the FIFO.

## Writes

- Every write is one register in one transfer, stored whole or not at all; the driver never writes a burst.
- The configuration is written in standby with deep standby off, the only state that takes every write (sections
  4.3.1, 4.3.8): ODR_CONFIG = standby with deep_dis = 1 and t_standby first, whenever the chip may be measuring or
  in deep standby, and ODR_CONFIG with the configured deep_dis last. It is read back in full; a power mode change
  goes through standby (section 4.3.7) and is read back. A forced measurement is polled until pwr_mode shows
  forced, at most t_startup (3 ms), before the driver waits for it, so a dropped write never returns the old data
  as new.
- A write that failed may have landed. A failed standby or mode write leaves the configuration in doubt
  (`IsConfigKnown()` false), and the calls that depend on it (`MeasureOnce()`, `WaitDataReady()`, the FIFO
  calls, `SetPowerMode()` to a measuring mode) return `StateUnknown` with nothing sent until `ApplyConfig()` or
  `EnsureInitialized()` succeeds; a running `EnsureInitialized()` with the configuration in doubt starts again
  from the soft reset. A failed or mismatching configuration sequence leaves the driver uninitialised (see
  [Resets](#resets)).
- A mode change is read back until it shows, at most t_startup_deep (4 ms), and in normal mode until
  OSR_EFF.odr_is_valid = 1 too: the datasheet gives no time for either. `ConfigRejected` only at the deadline.
- With the FIFO on, `ApplyConfig()` and `SetPowerMode()` return after t_reconf (3 ms): the change flushed the FIFO,
  and FIFO_COUNT is not to be read before the flush completes (section 4.6.4).
- `SetPowerMode(PowerMode::Standby)` and `Deinitialize()` are never refused once the driver has started: not
  after a reset was found, not with the configuration in doubt, not with a check pending. `Deinitialize()` writes
  ODR_CONFIG = standby with deep_dis = 1 and CMD = 0xB6, and is `Ok` only when INT_STATUS shows the reset.
- Every soft reset, at start-up and in `Deinitialize()`, is preceded by ODR_CONFIG = standby with deep_dis = 1 and
  t_standby: the datasheet does not say that deep standby takes CMD (section 4.3.1 says standby takes every
  write).

## Depends on

| Dependency | Version | Why |
| --- | --- | --- |
| A C++20 compiler | GCC 10, Clang 13 or later | Concepts constrain the port |
| CMake | 3.16 or later | Generates `bmp581_version.h` |
| Your port | | The bus within its requirements, CSB idle high on SPI; the delay; optional hooks ([Porting](porting.md)) |

Nothing else: no RTOS, no SDK, no other repository of the org.

## Threading

The driver is not thread-safe and takes no lock. Use each driver object from one task at a time, or guard every
call on it with one mutex; the layer that owns the driver owns that lock, and the bus lock when several chips
share a bus. Nothing may run in an ISR: every call can block for a bus transfer or a delay. Separate objects
share no state.

## Errors

`bmp581::Error` inside `bmp581::Result<T>`. Values 0 to 9 are the shared base set; this driver returns every one
of them except `Crc` (the bus carries no CRC), plus eight chip-specific values.

| `Error` | Value | Returned when |
| --- | --- | --- |
| `Ok` | 0 | The call succeeded |
| `NotInitialized` | 1 | `EnsureInitialized()` has not succeeded, or `Deinitialize()` ran or a reset was found since |
| `InvalidArgument` | 2 | A `Config` field, the address or an argument is out of range; nothing was sent |
| `Bus` | 3 | A port hook returned `false` |
| `Timeout` | 4 | NVM never ready, a forced measurement not ended, standby not reached, or no data ready in time |
| `DeviceIdMismatch` | 5 | CHIP_ID is not 0x50, or INT_STATUS did not show the soft reset (0x10, then 0x00) |
| `Crc` | 6 | Not returned |
| `Busy` | 7 | `MeasureOnce()` while the chip measures; nothing was sent |
| `DeviceFault` | 8 | STATUS reports an NVM error at start-up |
| `Unsupported` | 9 | `WaitDataReady()` without the data-ready source or in standby; a FIFO call with the FIFO off |
| `DeviceReset` | 16 | The chip reset, confirmed by a second read; the driver is uninitialised |
| `InvalidReply` | 17 | A reply the chip cannot send: a stuck, floating or disturbed line |
| `NoDevice` | 18 | An I2C address NACK, with `GetLastFailure()` |
| `NoData` | 19 | The data registers hold their reset value: no measurement since the reset |
| `OutOfRange` | 20 | A value outside the measurement range |
| `WriteMismatch` | 21 | A register read back differently from what was just written |
| `StateUnknown` | 22 | The configuration is in doubt after a failed write; nothing was sent |
| `ConfigRejected` | 23 | Normal mode: OSR_EFF.odr_is_valid still 0 at the deadline; the chip was put back in standby |

The [API guide](api-guide.md#handle-errors) says what to do about each.

## Memory

No heap, no exceptions, no RTTI, no `std::function`, no `std::string`, no `printf` and no static mutable state. A
driver object holds a reference to the port, a `Config`, a 96-byte read buffer and a few flags; an SPI driver also
holds two 97-byte frame buffers in its framing. The largest stack buffer is 12 bytes (a copy of the data frame).

## Platform

Platform-free: the same code runs on any target with a C++20 compiler, including the host tests. The ESP-IDF port
in [`examples/esp32`](examples.md) is an example, not part of the driver.

## Deviations from the standard

None.
