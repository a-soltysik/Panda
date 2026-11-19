# Panda benchmark

`panda-bench` is a standalone smoke benchmark for Panda's public frame and
performance APIs. Its workload creates and uploads a floor, cubes and spheres
before warmup and measurement, then creates an Engine with
`Engine::createWithSurface()` and renders that fixed colored scene through Panda's
normal frame path. `--lights N` sets the total number of lights: one directional
light and `N - 1` point lights. `--shadowed-lights N` sets how many of those lights
cast shadows; it defaults to `min(--lights, 65)`. Set both counts to zero for an
unlit scene. The default 65 shadow maps use a layered 2D-array image with 4096² atlas
pages, a 2048² directional map and 256² maps for each point-light face. Shadow atlas
pages grow to fit the requested workload, subject to device limits. See the
[benchmark specification](../docs/development/benchmarking.md) for measurement
boundaries and planned workloads.
The [UC benchmark smoke run](../docs/use-cases/benchmark-smoke-run.md) traces how
the executable validates settings, collects samples and writes its report.

## Build

Enable benchmarks when configuring Panda, then build the target from the
repository root. The benchmark needs `PANDA_BUILD_TOOLS=ON` for its window; the
plane, cube and sphere geometry helpers are included in Core.

Windows (MSVC):

```powershell
cmake --preset msvc-development -DPANDA_BUILD_BENCHMARKS=ON
cmake --build --preset msvc-development --target panda-bench
```

Linux (GCC):

```sh
cmake --preset gcc-development -DPANDA_BUILD_BENCHMARKS=ON
cmake --build --preset gcc-development --target panda-bench
```

The windowed smoke profile requires a graphical display and a Vulkan 1.3 device
with timestamp query support.

Note: the commands above produce a Debug build. Add `-DCMAKE_BUILD_TYPE=Release`
when configuring to produce an optimized build for performance comparisons.

## Run

Run from the repository root. Print the current options with:

```powershell
.\build-msvc-development\benchmarks\panda-bench.exe --help
```

```sh
./build-gcc-development-Linux/benchmarks/panda-bench --help
```

For example, request FIFO presentation:

```powershell
.\build-msvc-development\benchmarks\panda-bench.exe --profile smoke --output results --width 1280 --height 720 --lights 65 --present-mode fifo --warmup-seconds 1 --measure-seconds 3 --runs 1
```

```sh
./build-gcc-development-Linux/benchmarks/panda-bench --profile smoke --output results --width 1280 --height 720 --lights 65 --present-mode fifo --warmup-seconds 1 --measure-seconds 3 --runs 1
```

Options set the workload profile, output directory, requested window size, total
light count, shadow-casting light count, presentation mode, warmup and measurement
durations, and number of runs.
Presentation mode accepts `fifo`, `mailbox` and `immediate`; unsupported modes fail
instead of silently falling back. Defaults are FIFO, 65 total lights, 65
shadow-casting lights, 1 second warmup, 3 seconds measurement and one run. The smoke
profile is currently the only supported profile; `--lights` accepts 0 to 1000 and
`--shadowed-lights` accepts 0 to the total light count. For example,
`--lights 1000 --shadowed-lights 65` stresses the lighting loop while holding the
shadow-atlas workload at 65 lights. The report records both requested counts.

Each run records startup and teardown duration, CPU frame wall time, and GPU
elapsed time between the frame's timestamp markers. The paired per-frame CPU
and GPU samples and summary statistics are written to
`<output>/panda-bench-smoke.json`. The report also includes the host CPU model,
selected GPU, Vulkan driver and available build metadata. CPU and driver fields
are omitted when the host or driver does not provide them.

Engine logs are written to `<output>/panda-bench.log`; the console prints the
run summary and both output paths. A successful run exits with status 0. A
setup or controlled-run failure exits with status 1 and records the outcome in
the report when report writing is possible.
