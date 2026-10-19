# Frame rendering and benchmark smoke test

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Add two frame slots, descriptor/attachment reuse, the first clear/triangle path, and public performance data collection that ordinary applications and `panda-bench` can both use.

Non-goal: No complete scene, shadow system or measured performance promise.

Interface: Context-owned `PerformanceManager` for capabilities and asynchronous samples; the benchmark is a normal public-API client.

Paths: `include/core/panda/PerformanceManager.hpp`, `src/core/vulkan/performance/`, `src/core/vulkan/presentation/`, `tests/`, `benchmarks/` and directly affected documentation.
Specifications: [Frame reuse](../../design/contracts/resources.md#frame-and-window-state), [rendering](../../design/contracts/rendering.md#rendering-path), [benchmarks](../benchmarking.md).

## Acceptance

Capture a frame and retrieve its public GPU timestamp sample; test resize/skip/reuse and verify no routine device idle; run a short benchmark smoke profile through the public API.

Learning: Explain what rendering work the GPU execution duration covers and how its timestamps are collected.

## Current verification

- `cmake --build --preset msvc-quality --target panda_unit_performance_manager panda_unit_context panda_unit_core panda_unit_presentation --parallel 1` — passed, including formatting, clang-tidy and cppcheck checks.
- Selected Windows tests for device selection, Context, PerformanceManager and Presentation — 74/74 passed.
- `scripts/build-wsl.ps1 -Preset gcc-coverage -RunTests -Jobs 2` — 367/367 CPU tests passed (222 unit, 145 integration).
- Fresh GCC coverage after clearing generated `.gcda` profiles: 94.9% lines, 97.5% functions and 83.9% branches. Changed production files: `Context.cpp` 93.5% lines / 90.3% branches; `DeviceSelection.cpp` 92.3% / 80.8%; `PerformanceManager.cpp` 100% / 100%; `PerformanceManagerImpl.cpp` 100% / 97.4%; `Presentation.cpp` 87.9% / 73.9%.
- The CPU tests verify query recording, mocked readback and public sample delivery; they do not prove behavior on a real GPU. Frame capture and GPU checks for resize/skip/reuse remain pending. The public benchmark smoke profile is deferred to the benchmark change.
- Documentation checker and `git diff --check` passed after this update.
