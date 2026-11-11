# BMP581 host example

The driver on your PC, with no hardware: the host port in
[`host_bmp581_port.hpp`](host_bmp581_port.hpp) connects the real driver to a simulated BMP581 (the register model
the host tests use), and [`main.cpp`](main.cpp) initialises it, takes two forced measurements, then two in normal
mode with data ready, and shuts it down.

## Build and run

From the repository root, with CMake 3.16+ and a C++20 compiler:

```bash
cmake -S examples/host -B build-example
cmake --build build-example
./build-example/bmp581_host_example
```

## Expected output

```text
BMP581 driver 0.1.0 on the host
bmp581: initialised
101325.000 Pa  21.5000 C
101325.000 Pa  21.5000 C
101325.000 Pa  21.5000 C
101325.000 Pa  21.5000 C
bmp581: deinitialised
```

The host tests build this folder too and run the example (CTest `bmp581_host_example`), so CI fails if it stops
compiling or a call fails.

## Files

| File | What it is |
| --- | --- |
| [`main.cpp`](main.cpp) | The example: initialise, measure, stream, print |
| [`host_bmp581_port.hpp`](host_bmp581_port.hpp) | The host port: bus hooks over the simulated chip, `DelayUs()` and `Log()` |
| [`CMakeLists.txt`](CMakeLists.txt) | Standalone project; adds the driver with `add_subdirectory(../..)` |
