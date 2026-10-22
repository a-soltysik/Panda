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

- Windows: `cmake --build --preset msvc-fast --parallel` and
  `ctest --preset msvc-fast` passed. CTest registered 425 tests: 421 passed and
  four were skipped (the Linux-only buffered-close test and three unavailable
  noncoherent memory variants).
- WSL GCC: `cmake --build --preset gcc-fast --parallel` and
  `ctest --preset gcc-fast` passed. CTest registered 425 tests: 422 passed and
  three noncoherent memory variants were skipped because the device did not expose
  that memory type. The Linux buffered-write/close failure test passed.
- Unit coverage now exercises stale query availability while reusing a slot,
  confirms that CPU/GPU timestamps stay paired to the submitted frame, and checks
  acquire errors with untouched image output and report close failure.
- A Windows Release smoke run on an NVIDIA GeForce RTX 4070 Ti completed with 150
  paired CPU/GPU samples; this one-second run is not a performance baseline.
  A WSL smoke run produced 61 paired samples on llvmpipe, so it verifies the Linux
  reporting path rather than hardware GPU execution.
- The full build also builds `simple_scene`, `buffer_allocation`,
  `cuda_connection` and `panda-bench`. No new visual GPU inspection or
  RenderDoc capture was made.
- `python .agents/skills/panda-project-maintenance/scripts/check_docs.py` checked
  all 54 Markdown files and the task queue; `git diff --check` and Clang format
  validation passed.
