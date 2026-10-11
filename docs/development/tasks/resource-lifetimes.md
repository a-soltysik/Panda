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
CPU mapping. Native views and GPU submission remain planned.

Public Vulkan fields and window-surface signatures use Vulkan-Hpp flags and
borrowed `vk` handles. `makeVulkanError` accepts `vk::Result`. Internally, surface
queries and RAII adoption pass those handles directly; C types remain only at
external ABI boundaries. Context initializes one stable physical-device wrapper
before logical-device creation and shares it with its allocator.

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

Context, buffer and allocator slice, with CUDA disabled. Windows used MSVC
19.51.36260, Vulkan SDK 1.4.341.1, clang-tidy 22.1.8 and cppcheck 2.19. WSL Ubuntu
used GCC 16.0.1, Vulkan 1.4.341, clang-tidy 22.1.2 and cppcheck 2.19.

- Windows: `cmake --preset msvc-quality`, then
  `cmake --build --preset msvc-quality --target panda_unit_memory_pool panda_system_memory_allocation --parallel 2`
  passed with warnings-as-errors, clang-tidy and cppcheck.
  The memory-block and pool quality targets also passed both analyzers;
  `ctest --preset msvc-quality -R 'MemoryPool|MemoryBlock'`
  passed 52 unit cases. The memory-allocation system tests passed three coherent
  cases on NVIDIA GeForce RTX 4070 Ti
  (memory type 2), with no Vulkan validation errors. Noncoherent was skipped:
  no compatible memory type.
  A full `msvc-tests` build and `ctest --preset msvc-tests -LE system` passed 168/168.
- WSL: `scripts/build-wsl.ps1 -Preset gcc-quality -Target panda_unit_memory_pool,panda_system_memory_allocation`
  passed with the same quality checks. CTest in the managed mirror with
  `-R 'MemoryPool|MemoryBlock'` passed 52 unit cases. The memory-allocation system
  tests passed three coherent cases with no Vulkan validation errors, using
  llvmpipe (LLVM 21.1.8, memory type 0). Noncoherent was skipped: no compatible type.
  `scripts/build-wsl.ps1 -Preset gcc-sanitizers -RunTests` passed all 168
  noninteractive cases under ASan/UBSan with no findings.
- The naming-policy probe accepted `id` and rejected another short name through
  the real MSVC quality build; the temporary violation was removed.
- The four relevant CTest formatting cases passed in `msvc-quality`.
- `py -3 .agents/skills/panda-project-maintenance/scripts/check_docs.py` and
  `git diff --check` passed.

The Context/Buffer quality targets (`panda_unit_context`, `panda_system_buffer`,
`buffer_allocation`, `panda_core_verify_interface_header_sets`) passed on Windows
and WSL. Both platforms passed 21 Context lifecycle cases, including twelve cases
for description validation, shared ownership, moved Context/Buffer state, native
creation/allocation/binding rollback, dedicated teardown, intent selection and
checked Context destruction with live owners. Windows also passed seven focused
formatting cases. MSVC `/bigobj` is configured once for owned C++ targets without
propagating the option to consumers or other compiler languages.

The Hpp migration also passed the Tools, Error, surface/presentation, native
memory/window targets and `all_verify_interface_header_sets` on Windows and Linux.
Focused quality runs passed 37 behavior cases on each platform; Windows additionally
passed seven formatting cases. A consumer compile probe with ordinary Vulkan-Hpp
configuration accepts buffer flags and rejects assigning image flags to BufferDesc.

`ctest --test-dir <build> -R BufferSystem --verbose` (Windows `msvc-tests`, WSL
`gcc-quality`) passed all three
native memory-intent cases on Windows RTX 4070 Ti and WSL llvmpipe, without Vulkan
validation errors. The Windows run used process-local
`VK_LOADER_LAYERS_DISABLE=~implicit~` because an installed ReShade implicit layer
failed to load; the explicitly requested Khronos validation remained enabled.
The system assertion still checks every captured error diagnostic. The public
`buffer_allocation` executable creates a 4096-byte storage buffer, reports payload
size and device, and destroys the buffer before Context. Neither scenario submits
GPU work. The example ran successfully on Windows and Linux.

The combined native buffer, allocation and window checks passed eight available
cases on each platform, with three noncoherent allocation variants skipped for
unavailable memory types. Window checks additionally exercise clear-frame submission,
resize, suspension and teardown; they submit no work using the new Buffer owners.
Logs contain no Vulkan validation errors. The Linux evidence uses llvmpipe under
WSL, not a physical Linux GPU.

`scripts/build-wsl.ps1 -Preset gcc-docs -Target panda_docs -Jobs 2` passed the
Doxygen 1.15.0 API checks, including the new public Buffer declarations.

`scripts/build-wsl.ps1 -Preset gcc-coverage -RunTests -Jobs 2` passed 168/168 cases.
gcovr 7.2 with `gcov-16` measured Buffer and the five resource implementation files
(all 80 measured functions executed):

| Implementation | Executable lines | Raw compiler branches |
| --- | --- | --- |
| Buffer | 16/16 (100%) | 2/2 (100%) |
| BufferResource | 74/74 (100%) | 47/68 (69.1%) |
| MemoryAllocation | 51/51 (100%) | 19/30 (63.3%) |
| MemoryBlock | 128/128 (100%) | 146/208 (70.2%) |
| MemoryPool | 156/158 (98.7%) | 170/238 (71.4%) |
| RangeAllocator | 59/59 (100%) | 62/84 (73.8%) |
| Total | 484/486 (99.6%) | 446/630 (70.8%) |

The reports are local artifacts in `.cache/buffer-coverage/`. Raw branches
include compiler-generated exception edges; this is not source decision coverage.
The two unexecuted pool lines guard exhausted 64-bit ids and propagate a
non-exhaustion placement error from a previously validated existing block.
Tests do not mutate private state to force those guards. Fatal death-test paths
also cannot reliably flush child-process coverage counters.

A delivery audit also reports all instrumented production `src/` files from one
168-case run after resetting generated `.gcda` profiles: lines 1758/1928 (91.2%),
functions 257/260 (98.8%) and raw branches 1356/2179 (62.2%). All 129 executable
added/modified production `.cpp` lines execute. Relevant whole-file results are:

| Implementation | Executable lines |
| --- | --- |
| Context | 184/196 (93.9%) |
| DeviceSelection | 237/273 (86.8%) |
| Presentation | 292/362 (80.7%) |
| SwapchainGeneration | 115/116 (99.1%) |
| VulkanError | 45/46 (97.8%) |
| Window | 132/134 (98.5%) |

The complete report is a local artifact in `.cache/delivery-coverage/`. Unexecuted
existing capability/presentation/logging failure paths remain visible; mechanical
Hpp type changes do not establish coverage of those paths. Native smoke evidence
is separate from this CPU measurement. Controlled GLFW tests cover successful
Hpp surface conversion and native failure with a nonnull undefined output.
Ownership checkpoints reject destruction while a copy remains and require both
buffer destruction and dedicated memory freeing before the last reset completes.
Dedicated bind-failure rollback and InvalidArgument Vulkan status mapping are
also covered. No production hooks or weakened diagnostics were introduced.

Native noncoherent flush/invalidate and physical-GPU Linux behavior remain
unverified. Controlled Vulkan tests cover atom isolation/rounding, allocation/map
failure cleanup, wrapper flush/invalidate success and native-error propagation,
coherent request validation, preferred-type ranking and unsupported-type filtering,
partition separation, dedicated buffer/image metadata and rejected dedicated requests,
independent multi-heap budgets, empty-cache reclamation, and ownership/move/reset behavior.
The atom-placement fallback test verifies that the unusable temporary block is
unmapped and freed before the exact-size dedicated attempt.
Windows ASan and synchronization validation were not run for this slice; the
system scenario submits no GPU work and does not prove upload or retirement safety.

## Remaining work

Implement Texture/Mesh handles and uploads, then frame arenas, completion points,
recoverable Context close and GPU retirement. Native noncoherent
cache-operation validation, failed growth and in-flight replacement/release evidence,
an upload/GPU-use example and the maintainer learning handoff remain
required before task review. CPU passes do not establish GPU lifetime safety.
