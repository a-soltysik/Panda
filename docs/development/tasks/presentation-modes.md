# Presentation modes and frame pacing

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Let a windowed application choose Panda's presentation policy instead of fixing
every swapchain to FIFO. Expose supported FIFO, MAILBOX and IMMEDIATE behavior through
the public Context creation boundary, and report the effective mode. Make benchmark
runs select and record their presentation mode so refresh pacing is visible and runs
using different modes are not compared as the same workload.

Vulkan FIFO waits for vertical blanking and is the universally required mode.
MAILBOX also presents on vertical blanking, but replaces a pending request with the
newest frame. IMMEDIATE does not wait for vertical blanking and may tear. The API's
default and behavior when a requested mode is unsupported are decisions for this task;
the benchmark must report the effective mode and cannot silently label a fallback as
the requested mode. See the [Vulkan present-mode reference](https://docs.vulkan.org/refpages/latest/refpages/source/VkPresentModeKHR.html)
for exact mode semantics.

Current implementation always creates FIFO swapchains. `ContextOptions` has no
presentation setting. The accepted [benchmark specification](../benchmarking.md)
already requires reference reports to record the actual present mode.

Paths: `../../../include/core/panda/`, `src/core/vulkan/presentation/`, `benchmarks/`,
tests and directly affected documentation.
Specifications: [renderer](../../design/contracts/rendering.md#rendering-path),
[resource lifetimes](../../design/contracts/resources.md#frame-and-window-state),
[benchmarks](../benchmarking.md).

## Acceptance

- A windowed Context lets the application request a presentation policy; headless
  contexts remain unaffected.
- Unsupported requests fail clearly or follow a documented fallback, and the
  effective selected mode is observable.
- `panda-bench` can select and report its mode, including the effective mode; results
  with different modes are not compared as equivalent.
- Tests cover supported-mode selection and unsupported-mode handling.

Learning: Explain how FIFO, MAILBOX and IMMEDIATE affect frame pacing, image
replacement, tearing and the interpretation of CPU/GPU timing samples.
