# Allocation, uploads and resource lifetimes

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Implement typed handles, limited pooled allocator, upload batches, frame-safe parameter storage and completion-based retirement.

Non-goal: No CUDA export or per-object device idle.

Interface: Buffer/Texture/Mesh handles, UploadBatch and context-scoped CompletionPoint.

Paths: `../../../include/core/panda`, `src/core/vulkan/resources/`, `tests/cases/unit/`, `tests/cases/system/`, `tests/gpu/` and directly affected documentation.
Specifications: [Ownership and allocation](../../design/contracts/resources.md), [completion ordering](../../design/contracts/compute.md#submission-protocol).

Implement the CPU allocation algorithm first, then GPU allocation/upload, then
retirement and in-flight replacement. Verify each boundary before expanding.

## Acceptance

CPU split/coalesce/overflow tests; GPU copy-range ordering and visibility, noncoherent flush, failed growth and in-flight release tests with validation.

Learning: Explain why a live CPU handle and completed GPU use are separate conditions.

## Current implementation

The internal `RangeAllocator` manages byte ranges in one fixed-size block, without
Vulkan calls. Address-ordered first-fit placement preserves alignment padding as
free space; release coalesces adjacent ranges. A separate live-range index validates
exact releases. Recoverable failures preserve the block, and placement checks use
subtraction before addition to avoid 64-bit overflow. Metadata uses standard
containers and is not a frame arena.

`MemoryBlock` owns one ordinary or dedicated, non-exportable `VkDeviceMemory` allocation with
Vulkan-Hpp RAII. Creation checks the memory type, selected heap and maximum
allocation size, and rejects memory requiring unsupported features. Host-visible blocks are mapped
once for their lifetime. Noncoherent slices reserve whole atoms and use alignment
at least as strict as both the resource and atom requirements. Explicit
flush/invalidate validates a live slice and rounds the affected bytes within it;
coherent memory still validates requests but needs no cache call. The mapped view
is internal and does not authorize access while the GPU uses the range. Dedicated
blocks reserve their entire exact-size allocation at offset zero; a cache operation
may end at that allocation's boundary when it is inside the final atom.

Failed allocation never frees an undefined output; failed mapping frees the valid
allocation without unmapping. Destruction rejects outstanding ranges, then unmaps
before freeing memory, without a device-wide wait. Resource owners must complete
GPU use and destroy bound resources before releasing their ranges.

`MemoryPool` selects a compatible memory type, ranks explicit preferred properties
first and uses host coherence as a tie-breaker for host requests. Pool selection
and block creation share the same unsupported-property policy. It partitions ordinary blocks by memory type and buffers versus
optimal-tiled images, reuses existing ranges, and grows lazily within single-size,
heap-commitment and allocation-count limits. Required/preferred dedicated requests
and requests above half a block receive dedicated allocations. Failed ordinary
allocation or mapping permits one exact-size dedicated attempt; a lost device is
not retried. Existing live resources survive recoverable failure. At most one empty
ordinary block per partition is cached; budgets may reclaim empty caches too.

`MemoryAllocation` is a move-only RAII owner containing a borrowed pool pointer
and `id`. Destruction/reset delegates release to the pool. The pool outlives its
owners, and owners outlive GPU use and bound native resources. The wrapper has no
GPU retirement state: range lifetime follows ownership. The upload submission
layer retains staging and destination owners until GPU completion, then releases
them through normal RAII destruction. Pending-retirement accounting belongs to that layer.
Current counters cover this manager's native allocations, committed blocks and
live reserved ranges. Each Context owns one manager for Panda allocations.

`Context::createBuffer` publishes a copyable `Buffer` only after native creation,
memory-requirement selection, pooled/dedicated allocation and binding succeed.
Shared resource state destroys `VkBuffer` before its `MemoryAllocation` releases
the range. Context has stable heap-owned state; moving it preserves buffer owners.
`Context::createTexture` follows the same lifecycle for an optimal-tiled, single-mip,
single-layer 2D image: validate, create, query image memory requirements, allocate
device-local memory from the image partition, bind, then publish a copyable `Texture`.
Member order destroys `VkImage` before `MemoryAllocation` releases its range. Context
destruction drains submitted uploads before checking for external Buffer or Texture
owners and tearing down the device. Buffer memory intent selects device-local, upload
or readback memory, without exposing CPU mapping. Public native buffer views remain planned.

Public Vulkan fields and window-surface signatures use Vulkan-Hpp flags and
borrowed `vk` handles. `makeVulkanError` accepts `vk::Result`. Internally, surface
queries and RAII adoption pass those handles directly; C types remain only at
external ABI boundaries. Context initializes one stable physical-device wrapper
before logical-device creation and shares it with its allocator.

`CompletionPoint` identifies committed work without retaining Context or resources.
Context owns one submission clock shared by presentation and uploads, initialized
lazily without a window. Empty/foreign points are rejected; Context moves preserve
issued points and unsubmitted batches. Failed submit does not consume a value,
device loss stops new work, and outstanding values respect device limits. GPU
completion and presentation completion are tracked separately.

`UploadBatch` is a move-only borrower until submitted or destroyed. Each `write`
validates its destination and copies caller bytes into owned, flushed staging.
Ordered copies use conservative dependencies for earlier and later consumers.
Success consumes the public batch and transfers command/staging/destination owners
to Context's pending list. Only confirmed completion permits reclamation, through
ordinary RAII. Timeout and failed completion queries preserve pending owners.
Unsubmitted batch destruction cancels its writes. Context rejects live unsubmitted
batches and drains submitted packets before checking external buffer owners.

Presentation checks clock capacity before acquire. Failed acquire retirement
without a committed value rejects drain; current Context destruction treats
unsafe teardown as fatal. A timed-out committed retirement can be drained again.
The window example waits for its last frame at normal shutdown. The
[buffer example](../../../examples/buffer_allocation/README.md) creates a storage
buffer, uploads four values through the public API, waits and releases its owners.
Explicit buffer initialization policies and labels, texture views/uploads, mesh
handles/uploads and frame arenas remain planned.

The executable `panda_unit_range_allocator` demonstrates split/alignment, all six
release orders for three adjacent ranges, fragmented exhaustion, invalid requests
and releases, and 64-bit size/alignment boundaries. For example, allocating three
bytes then eight bytes aligned to sixteen leaves a reusable thirteen-byte gap;
alignment padding does not belong to the second allocation.

The executable `panda_system_memory_allocation` checks mapped blocks, two buffers
sharing a pool block, and dedicated allocation/reset with Vulkan validation.
Coherent and noncoherent variants report unavailable memory types as skips.
It submits no GPU work; upload visibility and in-flight retirement are not tested
by this scenario.

## Verification

The texture-creation slice was verified on Windows and in Ubuntu Linux WSL.

- `msvc-quality` built `panda_integration_context_resources`, `panda_system_texture`
  and `panda_core_verify_interface_header_sets`; the focused CTest run passed
  32/32 cases, including the Vulkan texture system test on Windows with validation.
- `gcc-quality` built the same targets in Ubuntu WSL; 31/31 integration cases and
  the texture system test passed.
- The documentation checker validated 44 Markdown files and the task queue;
  `git diff --check` passed.
- A full all-target build and a fresh project-wide coverage measurement were not
  run for this slice.
- Unit and integration cases cover range alignment/coalescing/overflow, pool
  growth fallback, noncoherent atom rounding and flush errors, upload preparation
  and submit failures, retry, overlapping copy order, timeout, completed-prefix
  reclamation, owner retention and Context destruction. Native noncoherent cache
  operations remain unverified because no tested GPU exposed a compatible type;
  controlled Vulkan callbacks cover that behavior. The documentation checker
  and `git diff --check` passed.

## Remaining work

Texture creation and ownership are implemented. Texture views/uploads, Mesh handles
and uploads, and frame arenas remain planned parts of this task. Native noncoherent
cache-operation validation needs a device exposing that memory type. The maintainer
learning handoff and acceptance of the remaining task scope are still pending;
passing tests do not replace that handoff.
