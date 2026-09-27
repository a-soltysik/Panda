# Public compute API

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Expose ordered Vulkan/CUDA registration, whole-buffer declarations, batching, pause/rebuild and error integration through public APIs.

Non-goal: No algorithm framework, backend translation or headless scheduler.

Interface: Context::add_compute, CudaSession::add_compute and lifecycle/error results.

Paths: `../../../include/core/panda`, `../../../include/core/panda`, `src/compute/`, `examples/compute/`, `tests/interop/` and directly affected documentation.
Specifications: [Compute](../../design/contracts/compute.md), [resource lifetime](../../design/contracts/resources.md#frame-contexts-and-retirement), [GPU clouds](../../design/contracts/rendering.md#gpu-cloud-representation).

## Acceptance

Mixed V/C workloads and lifecycle tests on both OSes; verify no per-frame import, CPU bounce or routine global wait.

Learning: Walk through a paused producer and the last-use completion point.
