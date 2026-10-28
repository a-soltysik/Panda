# Application-driven compute integration

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Build an application-owned multi-step compute workload on the single-buffer CUDA
submission path from the [external handoff](gpu-handoff.md). The application chooses
zero, one or several simulation steps before a rendered frame and submits the
selected work as a bounded batch. Panda keeps ownership of Vulkan resources and
rendering; it does not choose the algorithm, timestep, substep count or call
frequency.

Non-goal: No second submission API, persistent stage registration, automatic
per-frame callbacks, task graph, kernel translation or solver code.

Interface: Use `SharedCudaBuffer::submitWork` to enqueue each bounded CUDA batch
between the paired timeline wait and signal. Keep the public core free of CUDA types.

Paths: `include/core/panda/`, `include/cuda/panda/cuda/`, `src/cuda/`,
`examples/`, tests and directly affected documentation.
Specifications: [Compute and external buffers](../../design/contracts/compute.md),
[resource lifetime](../../design/contracts/resources.md#frame-contexts-and-retirement),
[GPU clouds](../../design/contracts/rendering.md#gpu-cloud-representation).

## Acceptance

Demonstrate an application choosing zero, one or several simulation steps before a
rendered frame; the selected steps are submitted as one batch, handed off to Vulkan
and retained through their final use. Test the graphics-only path, CUDA absence,
submission errors and safe shutdown on Windows/Linux. Verify there is no implicit
CPU bounce or recurring engine scheduler.

Learning: Follow one application call from host callback through GPU submission,
handoff and the first renderer read; distinguish enqueue success from completion.
