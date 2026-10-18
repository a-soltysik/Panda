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

- `cmake --build --preset msvc-tests --target panda_core --parallel` — passed.
- `scripts/build-wsl.ps1 -Preset gcc-development -Target panda_core` — passed.
- Documentation checker and `git diff --check` — passed.
- Frame capture, resize/skip/reuse checks, and the public benchmark smoke profile remain pending.
