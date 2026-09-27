# CUDA device matching and shared buffers

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Implement optional CUDA session, UUID match, dedicated exportable Vulkan buffer, platform handle ownership and one-time CUDA mapping.

Non-goal: No application solver or CPU bounce fallback.

Interface: CudaSession::create, CudaSession::create_buffer and SharedCudaBuffer::buffer.

Paths: `../../../include/core/panda`, `src/cuda/`, `src/vulkan/external/`, `tests/interop/` and directly affected documentation.
Specifications: [External buffers](../../design/contracts/compute.md#external-buffer-creation), [resource ownership](../../design/contracts/resources.md#ownership).

## Acceptance

Run the external buffer import and unavailable-runtime checks from the
[compute specification](../../design/contracts/compute.md#required-proof-before-engine-expansion)
on Windows/Linux. Record import counts and missing-driver startup; stop on an
unsupported handle/timeline combination.

Learning: Trace the owner of each OS handle and imported allocation.
