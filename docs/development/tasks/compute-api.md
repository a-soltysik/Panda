# Application-driven compute submission

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Expose the smallest useful way for an application to submit one bounded batch of
native compute work through the optional backend API. Start with CUDA and keep
application ownership of the algorithm, timestep, number of substeps and call
frequency. Provide a clear native extension point for applications that need more
control.

Non-goal: No persistent stage registration, automatic per-frame callbacks, task
graph, kernel translation or solver code.

Interface: To be selected after the [external backend handoff](gpu-handoff.md) is
implemented and proved. Keep the public core free of CUDA types.

Paths: `include/core/panda/`, `include/cuda/panda/cuda/`, `src/cuda/`,
`examples/`, tests and directly affected documentation.
Specifications: [Compute and external buffers](../../design/contracts/compute.md),
[resource lifetime](../../design/contracts/resources.md#frame-contexts-and-retirement),
[GPU clouds](../../design/contracts/rendering.md#gpu-cloud-representation).

## Acceptance

Demonstrate an application choosing zero, one or several simulation steps before a
rendered frame; the submitted work is batched, handed off to Vulkan and retained
through its final use. Test the graphics-only path, CUDA absence, submission errors
and safe shutdown on Windows/Linux. Verify there is no implicit CPU bounce or
recurring engine scheduler.

Learning: Follow one application call from host callback through GPU submission,
handoff and the first renderer read; distinguish enqueue success from completion.
