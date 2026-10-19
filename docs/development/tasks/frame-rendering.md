# Frame rendering and benchmark smoke test

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Add two frame slots, descriptor/attachment reuse, the first clear/triangle path, and public performance data collection that ordinary applications and `panda-bench` can both use.

Non-goal: No complete scene, shadow system or measured performance promise.

Interface: Context-owned `PerformanceManager` for capabilities and asynchronous samples; the benchmark is a normal public-API client.

Paths: `include/core/panda/PerformanceManager.hpp`, `src/core/vulkan/performance/`, `src/core/vulkan/presentation/`, `tests/`, `benchmarks/` and directly affected documentation.
Specifications: [Frame reuse](../../design/contracts/resources.md#frame-and-window-state), [rendering](../../design/contracts/rendering.md#rendering-path), [benchmarks](../benchmarking.md).

## Acceptance

Capture a frame and retrieve its public paired CPU/GPU sample; test resize/skip/reuse and verify no routine device idle; run a short benchmark smoke profile through the public API.

Learning: Explain the CPU frame wall duration boundary, what the GPU execution duration covers, and how its timestamps are collected.

## Current verification

- MSVC quality builds for the benchmark, Context, common library and examples — passed with format, clang-tidy and cppcheck checks.
- `ctest --test-dir build-msvc-quality -R "unit\.(Logger|FunctionName|HostInfo|Benchmark|DeviceSelection|Context|PerformanceManager|Presentation)" --output-on-failure` — 97/97 passed.
- `scripts/build-wsl.ps1 -Preset gcc-quality -RunTests -TestRegex unit` — full GCC quality build passed; 228 tests passed (225 unit tests and three format checks).
- Windows `Release` smoke run on an NVIDIA GeForce RTX 4070 Ti completed with 150 paired CPU/GPU frame samples. Schema 3 records the AMD Ryzen 5 7600 CPU, selected GPU, NVIDIA driver and startup/teardown durations. This validates the optimized execution and reporting path; the one-second run is not a performance baseline.
- Example builds passed for `simple_scene`, `buffer_allocation` and CUDA-enabled `cuda_connection`. Each example registers a `StreamSink` to `std::clog` before using Panda's logger.
- Frame samples pair CPU wall duration for Panda's complete frame call with asynchronous GPU execution duration; the CPU interval includes CPU-side waits and queuing presentation.
- `ctest --test-dir build-msvc-quality -R "CreatesValidatedSurfaceAndDevice" --output-on-failure` — passed on Windows with Vulkan validation. The real surface rendered successive frames, resized, suspended, then resumed and rendered again.
- A source/test search found no `waitIdle` or `deviceWaitIdle` calls.
- `scripts/build-wsl.ps1 -Preset gcc-development -Target panda-bench` — passed with GCC warnings treated as errors.
- WSL Linux smoke completed with 61 paired CPU/GPU samples and schema 3, but Vulkan selected `llvmpipe` (software rendering). WSL reports the RTX 4070 Ti through `nvidia-smi`, while its installed Vulkan ICDs do not expose NVIDIA; this verifies the Linux build and report path, not hardware GPU execution. A Linux smoke on a hardware Vulkan device remains pending.
- Unit tests cover mocked frame reuse, skipped presentation and failure paths.
- Documentation checker passed for 43 Markdown files; `git diff --check` passed.
