# Allocation, uploads and resource lifetimes

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Implement typed handles, a limited pooled allocator, upload batches and completion-based retirement.

Non-goal: No CUDA export or per-object device idle.

Interface: Buffer/Texture/Mesh handles, UploadBatch and context-scoped CompletionPoint.

Paths: `../../../include/core/panda`, `src/core/vulkan/resources/`, `tests/cases/unit/`, `tests/cases/system/`, `tests/gpu/` and directly affected documentation.
Specifications: [Ownership, allocation and completion](../../design/contracts/resources.md#handles-and-api-boundary).

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
`Context::createTexture` rejects invalid dimensions, formats and unsupported usage,
including usage that cannot support its required default image view. Before creating
the optimal-tiled, single-mip, single-layer 2D image, it checks the requested format,
usage and extent against the physical device. It then queries image memory
requirements, allocates device-local memory from the image partition, binds, creates
one default 2D image view and only then publishes a copyable `Texture`. The default
view spans the sole mip/layer and uses
the format's color, depth or stencil aspect; combined depth/stencil formats use depth.
Member order destroys `VkImageView` before `VkImage`, then `MemoryAllocation` releases
its range. Vulkan device, buffer and image-view handles remain private to the backend.
Queue submission and synchronization remain owned by Panda. Context destruction
drains submitted uploads before checking for external Buffer, Texture or Mesh owners
and tearing down the device. Buffer memory intent selects device-local, upload or
readback memory, without exposing CPU mapping.

`Context::createMesh` builds one fixed vertex buffer and, when described, one index
buffer from the same Context pool. `MeshDesc` uses a vertex count/byte stride and an
optional index count/type; index width is 16 or 32 bits. Both buffers use device-local
memory and transfer-destination usage. A failed second-buffer creation releases the
first through RAII before returning an error. The copyable `Mesh` handle reports its
counts and byte sizes without exposing its internal buffers. `UploadBatch::write`
accepts exact-size vertex and optional index byte spans, stages both before adding
either write to the batch, and keeps earlier batch entries if staging fails. The
caller supplies vertex attributes and valid index values; those are not part of this
storage handle.

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

`UploadBatch` is a move-only borrower until submitted or destroyed. Its first write
creates a mapped transfer-source staging page with a 4 KiB minimum. Later writes
share its remaining bytes, using a linear cursor; when a write does not fit, the
batch adds another page sized to at least that write. Buffer source slices need no
extra alignment. Texture slices align to the least common multiple of four bytes
and the format's texel-block byte size. Each `write` validates its destination,
snapshots caller bytes and flushes noncoherent memory before queuing the copy.
Ordered copies use conservative dependencies for earlier and later consumers.
Success consumes the public batch and transfers command, staging-page and
destination owners to Context's pending list. Only confirmed completion permits
reclamation, through ordinary RAII. A failed preparation or submission leaves the
batch's writes and pages available for retry. Timeout and failed completion queries
preserve pending owners. An in-flight page stays owned by its pending packet and
cannot be reused early. Cancellation or confirmed completion releases the batch's
staging pages through ordinary RAII and the memory pool. Pages are not cached across
batches. Unsubmitted batch destruction cancels its writes. Context rejects live
unsubmitted batches and drains submitted packets before checking external buffer
owners.

Texture uploads require transfer-destination and sampled usage. Full-image writes
and pitched rectangular writes accept single-plane 2D color formats, including
block-compressed formats. Region alignment and source size are validated before
staging allocation. A texture may be written only by one batch, which may enqueue
multiple regions; its batch identity is a private typed ID, not a pointer to the
batch implementation. Submission transitions the image from undefined through
transfer destination to shader-read layout. Unwritten texels remain undefined,
and a published sampled texture is immutable.

Presentation checks clock capacity before acquire. Failed acquire retirement
without a committed value rejects drain; current Context destruction treats
unsafe teardown as fatal. After a cleanup submission commits, presentation retains
the image index and completion until maintenance release succeeds; drain resumes
that work after a timeout. Present-failure recovery also retains the pending
semaphore replacement and image release. An unconfirmed release error keeps drain
failed. The window example waits for its last frame at normal shutdown.
[buffer example](../../../examples/buffer_allocation/README.md) creates a storage
buffer, uploads four values through the public API, waits and releases its owners.
Explicit buffer initialization policies and labels remain planned. Frame-local
transient storage belongs to the frame-rendering task; its representation will be
chosen against a representative workload rather than assumed to require an arena.

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

The resource and presentation-recovery changes pass the Windows and WSL checks.

- Windows: quality build and analysis, header/format checks, CUDA analysis, fast
  build and `ctest --preset msvc-fast` passed. CTest registered 427 tests: 423
  passed and four were skipped (the buffered-close test and three unavailable
  noncoherent memory variants).
- WSL GCC: quality build and analysis, header/format checks, CUDA analysis, fast
  build and `ctest --preset gcc-fast` passed. CTest registered 427 tests: 424
  passed; three noncoherent memory variants were skipped because the device did
  not expose that memory type.
- The documentation checker checked 60 Markdown files and the task queue; Mermaid
  validation accepted 18 diagrams; API documentation generation and
  `git diff --check` passed. Clang format checks passed on both platforms.

## Remaining work

Texture creation, internal image-view ownership, pitched/region uploads, Mesh
creation/uploads and per-batch staging pages are implemented. Reuse of staging pages
between batches and frame-local arenas are deferred until representative upload and
rendering workloads can be measured; they are not acceptance items for this task.
The maintainer accepted this task with the native noncoherent validation gap
recorded: controlled Vulkan callbacks cover flush range handling and failure
propagation, but no tested GPU exposed a compatible memory type for native
validation.
