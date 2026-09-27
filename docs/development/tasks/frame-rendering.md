# Frame rendering and benchmark smoke test

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Add two frame slots, descriptor/attachment reuse, a first clear/triangle path and the initial automated benchmark executable and report plumbing.

Non-goal: No complete scene, shadow system or measured performance promise.

Interface: Internal frame-slot renderer path and panda-bench smoke executable/report boundary.

Paths: `src/vulkan/frame/`, `src/renderer/`, `shaders/`, `tests/gpu/`, `benchmarks/` and directly affected documentation.
Specifications: [Frame reuse](../../design/contracts/resources.md#frame-and-window-state), [rendering](../../design/contracts/rendering.md#rendering-path), [benchmarks](../benchmarking.md).

## Acceptance

Capture a frame and timestamps; test resize/skip/reuse and verify no routine device idle; run a short benchmark smoke profile.

Learning: Identify exactly what the initial GPU timestamp measures.
