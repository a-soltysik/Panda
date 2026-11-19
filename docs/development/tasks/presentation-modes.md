# Presentation modes and frame pacing

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Let a windowed application choose Panda's presentation policy instead of fixing
every swapchain to FIFO. Expose supported FIFO, MAILBOX and IMMEDIATE behavior through
the public Engine creation boundary, and report the effective mode. Make benchmark
runs select and record their presentation mode so refresh pacing is visible and runs
using different modes are not compared as the same workload.

FIFO is the default. Panda uses the exact requested mode and returns `Unsupported`
when the window surface does not support it; headless Engines ignore this option.

Vulkan FIFO waits for vertical blanking and is the universally required mode.
MAILBOX also presents on vertical blanking, but replaces a pending request with the
newest frame. IMMEDIATE does not wait for vertical blanking and may tear.
[Vulkan present-mode reference](https://docs.vulkan.org/refpages/latest/refpages/source/VkPresentModeKHR.html).

`WindowRenderer::getEffectivePresentationMode()` reports the active swapchain
mode. `panda-bench --present-mode` requests a policy through `EngineOptions` and
the JSON report stores both requested and effective modes.

Paths: `../../../include/core/panda/`, `src/core/vulkan/presentation/`, `benchmarks/`,
tests and directly affected documentation.
Specifications: [renderer](../../design/contracts/rendering.md#rendering-path),
[resource lifetimes](../../design/contracts/resources.md#frame-and-window-state),
[benchmarks](../benchmarking.md).

## Acceptance

- A `Engine` lets the application request a presentation policy; headless
  contexts remain unaffected.
- Unsupported requests fail clearly or follow a documented fallback, and the
  effective selected mode is observable.
- `panda-bench` can select and report its mode, including the effective mode; results
  with different modes are not compared as equivalent.
- Tests cover supported-mode selection and unsupported-mode handling.

## Current verification

- Windows: `msvc-quality` build, interface-header isolation and format checks,
  `msvc-fast` build, and the full CTest suite passed. CTest registered 664 cases;
  659 passed and 5 were skipped, with no failures.
- Ubuntu WSL: `gcc-quality` build, interface-header isolation and format checks,
  `gcc-fast` build, and the full CTest suite passed. CTest registered 664 cases;
  656 passed and 8 environment-gated cases were skipped, with no failures.
- Documentation checks passed: the project checker validated 89 Markdown files,
  CMake validated 42 Mermaid diagrams in 93 files, and `gcc-docs` generated the
  public API documentation.
- An NVIDIA GeForce RTX 4070 Ti smoke run at 1920x1080 completed with 100 lights,
  65 shadowed lights and `immediate`. Its report records `immediate` as both the
  requested and effective mode and contains 93 paired frame samples. This one-second
  run verifies selection/reporting, not a performance baseline.
- An earlier three-run workload with the same light counts terminated once with
  Windows `APPCRASH` (`0xc0000005` in `ntdll.dll`); an identical retry completed.
  WER removed the minidump, so the source-level cause remains unknown.

Learning: Explain how FIFO, MAILBOX and IMMEDIATE affect frame pacing, image
replacement, tearing and the interpretation of CPU/GPU timing samples.
