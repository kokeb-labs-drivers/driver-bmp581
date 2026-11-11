# Testing and CI

How the driver is checked: host tests against a model of the chip, a fuzzer that corrupts what the chip sends
back, compiled documentation, an install test, ESP32 and Cortex-M builds, static analysis and measured quality
gates, on every pull request.

## Host tests

The tests build the real driver against a register model of the BMP581, on I2C and on SPI, with the firmware's
flags, `-fno-exceptions -fno-rtti`, and warnings as errors, on GCC and Clang.

```bash
cmake -S . -B build -DBMP581_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

| File | Covers |
| --- | --- |
| [`tests/test_bmp581.cpp`](../tests/test_bmp581.cpp) | Lifecycle, configuration (every field where section 7 puts it), power modes, forced and normal measurements, exact conversions, the range checks, the FIFO, interrupts, raw access, `Deinitialize()`, `Validate()` against table 7 |
| [`tests/test_reset.cpp`](../tests/test_reset.cpp) | A power-on reset found by every call, confirmed by a second read, never written over; a reset at every transfer of `ApplyConfig()`; pending checks; standby and `Deinitialize()` never refused |
| [`tests/test_bus_faults.cpp`](../tests/test_bus_faults.cpp) | Stuck, floating, looped-back and deaf lines at start-up and after; NACKs; lost acknowledgements; dropped and corrupt writes and reads at every transfer; the configuration left in doubt |
| [`tests/test_timing.cpp`](../tests/test_timing.cpp) | The open readings, each under every value: an NVM download that overwrites the marker (no false reset, a real one still found); the second data read of `Verify::ReadBack`; odr_is_valid 0, 600 and 3000 us late; a forced mode shown late; deep standby losing writes or not, over every flow; the FIFO flush wait; all 256 fills of the SPI selection frame; a reset during `ApplyConfig()` with its por read disturbed; standby after a failed first start |
| [`tests/fuzz/`](../tests/fuzz/fuzz_bmp581.cpp) | The fuzz target; CTest replays its corpus (`bmp581_fuzz_bmp581_corpus`), see below |
| [`tests/test_errors.cpp`](../tests/test_errors.cpp) | The base `Error` values are 0 to 9; every chip-specific value (16 and up) is returned by some call |
| [`tests/test_version.cpp`](../tests/test_version.cpp) | The version getters follow the generated header (also against a 9.8.7 probe) |
| [`tests/test_kconfig.cpp`](../tests/test_kconfig.cpp) | `KconfigDefaults()` with and without `CONFIG_` values |
| [`tests/compile_fail/`](../tests/compile_fail/port_without_delay.cpp) | Code that must not compile, each for its `EXPECT:` diagnostic: a port without `DelayUs()`, a `DelayUs(uint16_t)`, a misdeclared `GetLastFailure()`, an I2C or SPI hook with a narrowed parameter or without `noexcept`, a port of both buses, an invalid `CheckedConfig`, a class that is not a port |
| [`tests/mock_bmp581.hpp`](../tests/mock_bmp581.hpp) | The register model, with its own register map and a virtual clock; the I2C and SPI framing and the fault injection are in `mock_bmp581_bus.hpp` |
| [`tests/test_model.cpp`](../tests/test_model.cpp) | The model's register map, reset values, timing and tables against the driver's, with `static_assert`, and the model's own behaviour |
| [`tests/check_version_copy.cmake`](../tests/check_version_copy.cmake) | `_config/Doxyfile` carries the CMake version |
| [`examples/host/`](../examples/host/README.md) | The host example, built with the test flags and run (`bmp581_host_example`) |
| [`tests/check_install.cmake`](../tests/check_install.cmake) | Installs into a scratch prefix and builds [`tests/install_consumer/`](../tests/install_consumer/CMakeLists.txt) with `find_package(bmp581)` |
| [`tests/check_doxygen.cmake`](../tests/check_doxygen.cmake) | Doxygen with `_config/Doxyfile` and warnings as errors, when `doxygen` is installed |

### The model stands alone

[`tests/mock_bmp581.hpp`](../tests/mock_bmp581.hpp) is written from the datasheet, not from the driver: it does
not include `bmp581_registers.hpp` or `bmp581_types.hpp`. Its register map, field masks, reset values, timing and
tables are its own (namespace `bmp581_test::chip`, each with the datasheet section), so a wrong constant in the
driver cannot hide behind the same wrong constant in the model (handbook KB-TST-030).
[`tests/test_model.cpp`](../tests/test_model.cpp) compares the two at compile time:

```cpp
--8<-- "tests/test_model.cpp:cross-check"
```

### The model changes state on its own

A model that changes only when the driver writes it cannot catch a missed reset. This one, on its virtual clock
(`Elapse()`, driven by the mock port's `DelayUs()` and by every frame):

- resets mid-session (`PowerOnReset()`, or before any chosen transfer): every register back to its reset value,
  INT_STATUS.por set, the FIFO empty, the interface back in I2C mode, no answer for its boot time, then
  STATUS.status_nvm_rdy;
- completes measurements in time: forced mode returns to standby after the start-up and the conversion (longer
  from deep standby), normal mode measures once per data rate period, continuous mode back to back; each updates
  the data registers, sets drdy, fills the FIFO (streaming or stop-on-full, with decimation) and runs the
  out-of-range count;
- completes a commanded standby after a delay, reports the mode it is in, loses writes to the standby-only
  registers outside standby, and NACKs the soft reset on I2C;
- reads FIFO_DATA without incrementing the address, keeps a partly read frame, and returns 0x7F when empty;
- offers both readings where the datasheet is open: the standby read-back at once or after the transition
  (`standby_delay_us`), INT_STATUS showing disabled sources or not (`flags_for_disabled_sources`), a new mode
  (forced included) shown late (`mode_lag_us`), OSR_EFF.odr_is_valid shown late on its own (`odr_valid_lag_us`),
  deep standby losing every write but ODR_CONFIG and CMD or not (`deep_standby_drops_writes`, on by default: the
  strict reading), the NVM download of a measurement from deep standby reaching NVM_DATA or not
  (`nvm_download_rewrites_data`, with the value it leaves, `nvm_download_data`: the tests use 0x0000, another
  word and the marker itself), and what MISO shows in the SPI selection frame (`select_fill`); the reset and
  doubt tests run under both, and `tests/test_timing.cpp` runs every flow under each.

The bus mock adds the faults: an address NACK on any transfer, a data NACK part-way, a lost acknowledgement, a
dropped write, a corrupt byte in any read, a line stuck low, floating high, looped back (SPI) or deaf from any
transfer on.

## Fuzz tests

Everything the driver reads comes from the chip over the bus, so the data read is what a noisy cable, a wrong part
or a broken chip controls. [`tests/fuzz/fuzz_bmp581.cpp`](../tests/fuzz/fuzz_bmp581.cpp) lets libFuzzer choose the
bus, a configuration, then up to 64 driver calls, and on every bus transfer what the bus does: pass it to the
model, fail it, lose its acknowledgement, flip bits of the data read or drop an acknowledged write, replace the
data read by a stuck or floating line, reset the chip just before it, or let 2 ms pass first. The chip may answer
at once after a reset or stay silent for its boot time, and on SPI the first frame after a reset only selects SPI
again, with the data the fuzzer chooses. Between calls it lets time pass, changes the pressure and temperature
(the reset value and out-of-range values included), resets the chip and flips the model's open readings (the
standby delay, disabled sources, deep standby losing writes, the NVM download reaching NVM_DATA and the value it
leaves, the mode and odr_is_valid lags, the SPI selection fill). After every call it checks the driver's
promises and aborts, naming the line, when one breaks:

- a call returns an `Error` the enum defines; a failed value is `T{}`; `InvalidArgument` sent nothing; before the
  first start nothing but `Deinitialize()` makes a transfer, and standby once a start has reached the chip;
  `DeviceReset` leaves the driver uninitialised;
- no stuck-line value is ever data: a call whose data read was replaced by all 0x00 or all 0xFF never returns `Ok`;
- every measurement and FIFO sample returned `Ok` is inside the measurement range and never the reset value, and
  equals what the chip sent when no read of the call was tampered with;
- a reset is never missed: after a power-on reset no call that checks for one returns `Ok`, except a call whose own
  fault (a flipped or replaced read, the reset itself) it cannot see, and the next call is checked again;
- nothing is written over a reset chip except in the call during which the reset happened or a read was tampered
  with, and in `EnsureInitialized()`, which starts from the soft reset;
- both reset promises hold on the call after one in which the chip reset and a read was disturbed: the marker in
  NVM_DATA, which no call but the start writes, shows the reset that the call's own writes hid from the sentinel
  (see [Contract](contract.md#resets));
- every log line is a non-empty plain-text message.

```bash
# libFuzzer needs Clang. On macOS, Homebrew's LLVM: link its libc++, and add -Wl,-ld_classic.
cmake -S . -B build-fuzz -DCMAKE_CXX_COMPILER=clang++ -DBMP581_BUILD_TESTS=ON -DBMP581_BUILD_FUZZERS=ON
cmake --build build-fuzz --target fuzz_bmp581
./build-fuzz/tests/fuzz/fuzz_bmp581 -max_total_time=300 -max_len=512 tests/fuzz/corpus/fuzz_bmp581
```

The same file also builds without libFuzzer as `fuzz_replay_fuzz_bmp581`, and CTest replays every input in
[`tests/fuzz/corpus/fuzz_bmp581/`](../tests/fuzz/corpus/fuzz_bmp581) on GCC and Clang
(`bmp581_fuzz_bmp581_corpus`): the seeds, and every input that ever broke a promise, so a fixed bug stays fixed:
`check-overwrote-data` (the reset check read into the buffer a measurement was still decoded from),
`invalid-argument-after-check` (an argument refused after a pending check had sent a read),
`reset-hidden-by-failed-sequence` (a reset and a corrupt INT_STATUS read inside a failing configuration sequence,
now a restart), `reset-and-disturbed-read-in-one-call` (a reset covered by the sequence's own writes, now found
by the marker),
`sentinel-rewritten-then-setter` (after such a call the next setter wrote before it read the por;
setters now read INT_STATUS first, and the whole configuration decides a por the sentinel contradicts) and
`por-on-soft-reset`
(a power-on reset on the soft reset write itself, a fault of the model's bookkeeping, not of the driver) and
`standby-after-failed-start` (standby sent after a failed first start that reached the chip, as it must be) and
`stuck-second-data-read` (the second data read of `Verify::ReadBack` stuck low, outvoted by the third: a correct
value, which the invariant first took for stuck-line data). CI
fuzzes for 120 s on every pull request, from the seeds plus a corpus it keeps between runs (job
`Host quality / fuzz`).

## Quality gates

The `Host quality` job measures the library code, `inc/` and `src/` only (tests and examples are left out), and
fails when a value is over the limits of this repository's assurance level, `high` (set in
[`.kokeb/handbook.yml`](../.kokeb/handbook.yml), defined by the handbook's ADR-0020):

| Gate | Tool and how it is measured | Limit at `high` |
| --- | --- | --- |
| Complexity | lizard 1.24.1, modified cyclomatic complexity (`-m`: a `switch` counts once) per function | CCN at most 15 |
| Function length | lizard, lines of code per function | at most 60 |
| Parameters | lizard, per function | at most 6 |
| Line coverage | GCC 14 `--coverage -O0`, every CTest, gcovr 8.7 `--merge-lines` (a template's lines count once across its instances), `--exclude-unreachable-branches --exclude-throw-branches` | at least 85 % |
| Branch coverage | same run | at least 75 % |
| MC/DC | GCC `-fcondition-coverage`, same run | measured, not gated at `high` (100 % at `safety`) |
| Sanitizers | Clang, `-fsanitize=address,undefined -fno-sanitize-recover=all`, every CTest | no report |
| Fuzzing | libFuzzer with ASan and UBSan, 120 s per target | no broken promise |

The reports are in the run's `host-quality-reports` artifact and its step summary. To run the gates locally:

```bash
# Coverage (GCC 14 or later), then the complexity limits
cmake -S . -B build-cov -DCMAKE_CXX_COMPILER=g++-14 -DBMP581_BUILD_TESTS=ON \
  -DCMAKE_CXX_FLAGS="--coverage -O0 -fcondition-coverage"
cmake --build build-cov && ctest --test-dir build-cov
gcovr --root . --filter inc/ --filter src/ --merge-lines --exclude-unreachable-branches --txt build-cov
lizard -m -C 15 -L 60 -a 6 inc src

# Sanitizers (Clang)
cmake -S . -B build-san -DCMAKE_CXX_COMPILER=clang++ -DBMP581_BUILD_TESTS=ON \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -O1 -g"
cmake --build build-san && ctest --test-dir build-san --output-on-failure
```

The fuzz gate runs locally as shown in [Fuzz tests](#fuzz-tests).

clang-tidy 20 ([`_config/.clang-tidy`](../_config/.clang-tidy): bugprone, CERT, clang-analyzer,
cppcoreguidelines, modernize, performance, portability and readability, each disabled check with its reason) and
cppcheck (strict) run in the `C++ lint` and `Repository checks` jobs and fail on any finding.

The `MISRA` job ([`ci-misra.yml`](../.github/workflows/ci-misra.yml)) runs the shared `ru-misra` workflow of
ci-tools at level `high`: it traces a host build and the Cortex-M build (`bmp581_optimised_build`) of every
translation unit, checks them against MISRA C++:2023 and the handbook's C++20 profile (the CodeQL coding-standards
and profile packs, plus the validated clang-tidy and cppcheck checks), and reports each finding against the
handbook's compliance matrix and deviation records. A finding is fixed in the code; a deliberate exception names
its deviation on the line before it (`// codeql::misra_deviation_next_line(MD-CPP-009)` above the `.ipp` include
of `inc/bmp581.hpp`). Until the repository is public the job runs with `open_source_prerelease: true`, the
declaration that the code is released under the licence in `LICENSE` and is to be published.

## Documentation snippets

Every C++ block on this site is included from [`tests/docs/`](../tests/docs/my_port.hpp), which the host build
compiles (target `bmp581_doc_snippets`) with the driver instantiated over the documented port. A change to
the API that breaks a page breaks the build first. The README's example is a generated copy of the same file;
`kokeb-docs check` fails when it drifts.

## CI workflows

Everything runs from [`ci.yml`](../.github/workflows/ci.yml), which calls one workflow file per check and ends in
the `CI gate` job, the single required check (`CI gate / gate`).

| Job | What it checks |
| --- | --- |
| Host tests | GCC, then Clang (one job), build and run every test above, then build and run `examples/host` on its own |
| Host quality | The gates above: complexity, coverage, sanitizers, fuzzing |
| Cross-compile | `arm-none-eabi-g++` for Cortex-M0+, M4F and M7F with the strict flags, warnings as errors (nothing linked) |
| C++ lint | clang-format and clang-tidy over the sources |
| MISRA | MISRA C++:2023 and the C++20 profile on traced host and Cortex-M builds, at level `high` (see [Quality gates](#quality-gates)) |
| Docs | Strict site build (`_config/mkdocs.yml`), figure and generated-region drift, Doxygen (`_config/Doxyfile`), offline link check, Lighthouse |
| Repository checks | One job: DCO on pull requests, the org standard, Markdown and YAML lint, the [Kokeb Labs handbook](https://github.com/kokeb-labs/handbook) rules for `.kokeb/handbook.yml` (by rule ID), template drift (warns only) and cppcheck over the driver sources; a second job runs the handbook's source checks (include guards, bus requirements, raw access) |
| CI gate | Passes only if every job above passed: the one required check |

The **ESP32 examples** job builds every app in `examples/esp32/app_config.yml`, for each listed ESP-IDF version and
build type, with the repository root as the driver's component, and lints the example sources. The gate needs it
too.

The workflows call the shared, versioned workflows of
[kokeb-labs/ci-tools](https://github.com/kokeb-labs/ci-tools) at `@v1`.

## On hardware

Nothing here has run on a BMP581 yet: every behaviour is **not verified on hardware**. The bench steps, on the
ESP32 example ([Examples](examples.md)) or any port, each with what passes:

1. `EnsureInitialized()` on I2C at `0x46` and `0x47`, and on SPI: `Ok`, `GetRevision()` 0x32. Log the reads of the
   start-up: INT_STATUS 0x10 then 0x00; STATUS with status_nvm_rdy set on the first poll or within a few.
2. Read 0x23 to 0x26 with `ReadRegister()` in each power mode: 0x00 every time (the driver refuses a data frame
   otherwise).
3. Write DSP_IIR with a pressure coefficient through `ApplyConfig()`: it reads back with set_iir_p in bits 5..3, as
   the register map says (the field description gives offset 0 for both).
4. `MeasureOnce()` at x1/x1 and at x128/x128, from deep standby and from standby: `Ok`, and the time between the
   forced write and pwr_mode reading 0 within `ForcedMeasurementMaxUs()`. Breathe on the chip: the temperature
   rises; lift it 1 m: the pressure falls by about 12 Pa.
5. Normal mode at a table 7 limit (x8/x1 at 140 Hz): `ConfigRejected` never, OSR_EFF bit 7 set; one step above the
   table (rejected by `Validate()`) written by hand: OSR_EFF bit 7 clear. Note when the bit is updated (at the ODR
   or OSR write, or at the mode change).
6. A brown-out: pull VDD below 1 V for 10 ms while streaming. The next `ReadMeasurement()` or `WaitDataReady()`
   returns `DeviceReset`, and `EnsureInitialized()` starts the chip again. On SPI, the first frame after the
   reset reads garbage and the call still ends in `DeviceReset`.
7. FIFO: `FifoPreset()` for 2 s, then `ReadFifo()`: 16 frames, each `Ok`; a FIFO left until it overflows in
   stop-on-full mode keeps the oldest frames.
8. SPI at 10 MHz with the CSB setup time of your MCU: no `InvalidReply` over 10 000 reads.
9. Deep standby and writes (section 4.3): after a soft reset, write OOR_RANGE through the port (outside the
   driver) and read it back with `ReadRegister(0x34)`, once straight away and once after ODR_CONFIG = standby with
   deep_dis = 1 and 2.5 ms. Record whether the first write lands; the driver assumes it may not and always takes
   the second path.
10. odr_is_valid timing: log OSR_EFF on every poll after `SetPowerMode(PowerMode::Normal)` at a valid setting.
    Record the poll on which bit 7 first reads 1; it must be within 4 ms (the driver's deadline).
11. Forced mode timing: log ODR_CONFIG on every poll after the forced write. Record the first poll that shows
    pwr_mode = forced; it must be within t_startup (3 ms).
12. The NVM_DATA marker: start the driver, then run `MeasureOnce()` from deep standby 1000 times and leave normal
    mode with deep standby (`LowPowerPreset()`) for 10 minutes. Record what `ReadRegister(0x2C)` and `0x2D` read
    (0x3C and 0xC3, or an NVM word, 0x0000 included: the NVM is downloaded at the start of each measurement from
    deep standby, section 7.22). Either way `ApplyConfig()` never returns `DeviceReset` (the marker is verified
    or written before each sequence), and a brown-out clears both to 0x00 and gives `DeviceReset`.
13. SPI selection frame: with and without the MISO pull-up, log the bytes of the first frame after a brown-out:
    0xFF with the pull-up; whatever they are, the next call returns `DeviceReset`.
14. FIFO flush: with `FifoPreset()`, read FIFO_COUNT right after `SetPowerMode(PowerMode::Continuous)` returns
    (it waits t_reconf): 0 or a count that grows from there, never a stale count.
15. The soft reset from deep standby: with the default `Config` (deep standby), send CMD = 0xB6 through the port
    without the ODR_CONFIG write before it, and read INT_STATUS: record whether por is set. The driver writes
    ODR_CONFIG = standby with deep_dis = 1 first either way.
16. `ReadMeasurement(Verify::ReadBack)` 100 000 times in continuous mode at the fastest rate: never
    `InvalidReply` on a clean bus.

Record the board, the bus, the steps run and their result in the pull request's bench-status section.
