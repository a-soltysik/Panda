# Allocation, uploads and resource lifetimes

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Implement typed handles, limited pooled allocator, upload batches, frame-safe parameter storage and completion-based retirement.

Non-goal: No CUDA export or per-object device idle.

Interface: Buffer/Texture/Mesh handles, UploadBatch and context-scoped CompletionPoint.

Paths: `../../../include/core/panda`, `src/vulkan/resources/`, `tests/cases/unit/`, `tests/gpu/` and directly affected documentation.
Specifications: [Ownership and allocation](../../design/contracts/resources.md), [completion ordering](../../design/contracts/compute.md#submission-protocol).

Implement the CPU allocation algorithm first, then GPU allocation/upload, then
retirement and in-flight replacement. Verify each boundary before expanding.

## Acceptance

CPU split/coalesce/overflow tests; GPU alignment, noncoherent flush, failed growth and in-flight release tests with validation.

Learning: Explain why a live CPU handle and completed GPU use are separate conditions.
