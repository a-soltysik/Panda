# Vulkan and CUDA synchronization

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Implement retained native callbacks, G/C timelines, release/acquire, Vulkan completion join and fault ledger; prove two frames and mixed stages.

Non-goal: No general task graph or speculative overlap.

Interface: VulkanStageDesc/CudaStageDesc, StageRegistration and the G/C completion protocol.

Paths: `src/compute/`, `src/cuda/`, `tests/interop/` and directly affected documentation.
Specifications: [Submission and faults](../../design/contracts/compute.md#submission-protocol), [frame lifecycle](../../design/contracts/resources.md#frame-and-window-state).

First verify producer-before-wait and fault bookkeeping with a CPU call log. Then
prove native handoff on the GPU, followed by unregister/rebuild/failure paths.

## Acceptance

Ordered GPU access, retirement/replacement and submission failure checks with tagged GPU data, validation, supervised failure timeouts and Nsight ordering on Windows/Linux.

Learning: Draw every signal producer and wait consumer for two consecutive writes.
