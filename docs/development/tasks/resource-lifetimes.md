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

CPU split/coalesce/overflow tests; GPU alignment, noncoherent flush, failed growth and in-flight release tests with validation.

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
GPU retirement state: range lifetime follows ownership. The future submission
layer will retain resource owners until GPU completion, then release them through
normal RAII destruction. Pending-retirement accounting belongs to that layer.
Current counters cover this manager's native allocations, committed blocks and
live reserved ranges. Each Context owns one manager for Panda allocations.

`Context::createBuffer` publishes a copyable `Buffer` only after native creation,
memory-requirement selection, pooled/dedicated allocation and binding succeed.
Shared resource state destroys `VkBuffer` before its `MemoryAllocation` releases
the range. Context has stable heap-owned state; moving it preserves buffer owners.
Context destruction rejects live owners before tearing down the device. Buffer
memory intent selects device-local, upload or readback memory, without exposing
CPU mapping. Native buffer views and resource submission remain planned.

Public Vulkan fields and window-surface signatures use Vulkan-Hpp flags and
borrowed `vk` handles. `makeVulkanError` accepts `vk::Result`. Internally, surface
queries and RAII adoption pass those handles directly; C types remain only at
external ABI boundaries. Context initializes one stable physical-device wrapper
before logical-device creation and shares it with its allocator.

`CompletionPoint` identifies successfully committed work without retaining Context
or resources. The presentation timeline is owned by an internal `SubmissionQueue`;
frame and acquire-retirement submissions use its single monotonic clock. Failed
submit does not consume a value. `FrameResult::completion` is empty for skipped
or suspended frames. `Context::poll` queries GPU completion, and `Context::wait`
takes a nonnegative nanosecond timeout with distinct Completed/Timeout outcomes.
Empty and foreign points are rejected. Context moves preserve issued points;
headless contexts currently issue no points. Device loss stops further submissions
and waits. Outstanding values respect the physical device's timeline limit; presentation
checks available capacity before acquiring a swapchain image. Failed acquire
retirement without a committed value rejects drain; current Context destruction
treats this unsafe teardown as a fatal contract failure. A timed-out committed
retirement can be drained again.
The window example keeps the last issued point and explicitly waits at normal
shutdown without adding a CPU wait after each frame.
This layer currently serves presentation; uploads and retained resource packets
remain planned. Completion of this clock does not mean display completion.

The [buffer allocation example](../../../examples/buffer_allocation/README.md)
uses public creation, resource diagnostics and RAII shutdown without a window, uploads or submitted
GPU work. Explicit initialization, labels, recoverable close and retained
in-flight use remain planned.

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

Current allocator, Buffer and completion infrastructure, with CUDA disabled.
Windows used MSVC, Vulkan SDK 1.4.341.1, clang-tidy 22.1.8 and cppcheck 2.19;
Linux used GCC 16.0.1, Vulkan 1.4.341, clang-tidy 22.1.2 and cppcheck 2.19
in the managed Ubuntu WSL environment.

- Windows: the complete `msvc-quality` build passed warnings-as-errors,
  clang-tidy, cppcheck and formatting. `ctest --preset msvc-quality -LE system`
  passed 217 cases: 189 CPU behavior cases and 28 formatting checks.
- Linux: `scripts/build-wsl.ps1 -Preset gcc-quality -RunTests -Jobs 2`
  built all quality targets and ran 230 cases without failures, including 189 CPU
  behavior cases, formatting and 13 native cases. Three noncoherent allocation
  variants were skipped because no compatible memory type was available.
- `all_verify_interface_header_sets` passed on both platforms, including the new
  CompletionPoint header. The `msvc-unity` and `gcc-unity` builds passed for Core,
  Tools and examples. `gcc-docs` generated the Doxygen API without errors.
- `gcc-sanitizers` passed all 189 noninteractive cases under ASan/UBSan, with no
  findings. Windows ASan was not run for this slice.
- `WindowedContextSystem` passed both cases on Windows RTX 4070 Ti and Linux
  llvmpipe (LLVM 21.1.8). The scenario waits and polls real frame points, including
  one issued before moving Context, and checks resize, suspension and teardown.
  Verbose logs contain no Vulkan validation errors. Windows disabled only implicit
  layers in the test process because the installed ReShade layer failed to load;
  the explicitly requested Khronos validation remained enabled. Synchronization
  validation and a physical Linux GPU were not verified.
- The window example compiles on both platforms and waits for the last committed
  frame at normal shutdown. The buffer example still creates and releases a
  4096-byte storage buffer without submitting work that uses it.
- The documentation checker and `git diff --check` passed.

CPU tests cover range alignment/coalescing/overflow, atom isolation and cache
rounding, pool selection/budgets/fallback, shared Buffer ownership and rollback,
and destruction order. The completion cases additionally cover successful commit,
failed-value reuse, timeout and cached completion, empty/foreign points, moved
Contexts, device loss, invalid counter reads and outstanding-value limits.
Presentation tests check capacity before acquire, submit-failure retirement,
failed retirement rejecting drain, timeout retry, and empty/committed points
when swapchain recreation is required.

`gcc-coverage` passed 189 CPU cases after clearing generated profiles. gcovr 7.2
with `gcov-16` measured production `src/` coverage: 1896/2041 executable lines
(92.9%), 276/279 functions (98.9%) and 1515/2359 raw compiler branches (64.2%).
The local report is `.cache/delivery-coverage/`.

| Implementation | Executable lines | Raw compiler branches |
| --- | --- | --- |
| CompletionPoint | 3/3 | 3/4 |
| SubmissionQueue | 114/115 | 87/124 |
| Context | 192/204 | 147/213 |
| Presentation | 305/349 | 231/420 |

All 160 other executable added/modified production lines run; the one unexecuted
line rejects an exhausted 64-bit submission counter. Tests do not mutate private
state to force that guard. Raw branches include compiler-generated exception
edges and are not source decision coverage. Fatal child-process paths cannot
reliably flush coverage counters. Existing unexecuted presentation/capability paths
remain visible in the whole-file report.

Buffer and the five allocator/resource implementation files retain 484/486
executed lines, with all 80 measured functions executed. The two pool guards
cover exhausted ids and a non-exhaustion placement error from a previously
validated existing block. Native allocation cases establish coherent mapping,
binding and release; controlled callbacks cover noncoherent cache behavior.
Native noncoherent flush/invalidate, resource upload visibility and in-flight
Buffer replacement/release remain unverified. Passing frame completion checks
establish no submitted use of Buffer owners. The completion/lifetime learning
checkpoint is confirmed; upload/retirement learning and full task acceptance
remain outstanding.

## Remaining work

Implement Texture/Mesh handles and uploads, then frame arenas,
recoverable Context close and GPU retirement. Native noncoherent
cache-operation validation, failed growth and in-flight replacement/release evidence,
an upload/GPU-use example and the maintainer learning handoff remain
required before task review. CPU passes do not establish GPU lifetime safety.
