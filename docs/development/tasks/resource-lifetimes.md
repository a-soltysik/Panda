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
live reserved ranges; Context will own one manager for all Panda allocations.

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

Range, memory-block and pool slice, with CUDA disabled. Windows used MSVC
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
  A full `msvc-tests` build and `ctest --preset msvc-tests -LE system` passed 154/154.
- WSL: `scripts/build-wsl.ps1 -Preset gcc-quality -Target panda_unit_memory_pool,panda_system_memory_allocation`
  passed with the same quality checks. CTest in the managed mirror with
  `-R 'MemoryPool|MemoryBlock'` passed 52 unit cases. The memory-allocation system
  tests passed three coherent cases with no Vulkan validation errors, using
  llvmpipe (LLVM 21.1.8, memory type 0). Noncoherent was skipped: no compatible type.
  `scripts/build-wsl.ps1 -Preset gcc-sanitizers -RunTests` passed all 154
  noninteractive cases under ASan/UBSan with no findings.
- The naming-policy probe accepted `id` and rejected another short name through
  the real MSVC quality build; the temporary violation was removed.
- The four relevant CTest formatting cases passed in `msvc-quality`.
- `py -3 .agents/skills/panda-project-maintenance/scripts/check_docs.py` and
  `git diff --check` passed.

`scripts/build-wsl.ps1 -Preset gcc-coverage -RunTests` passed 154/154 cases.
gcovr 7.2 with `gcov-16` measured the four allocator implementation files:

| Implementation | Executable lines | Raw compiler branches |
| --- | --- | --- |
| MemoryAllocation | 51/51 (100%) | 19/30 (63.3%) |
| MemoryBlock | 128/128 (100%) | 146/208 (70.2%) |
| MemoryPool | 156/158 (98.7%) | 169/238 (71.0%) |
| RangeAllocator | 59/59 (100%) | 62/84 (73.8%) |
| Total | 394/396 (99.5%) | 396/560 (70.7%) |

The reports are local artifacts in `.cache/pool-coverage-fixed/`. Raw branches
include compiler-generated exception edges; this is not source decision coverage.
The two unexecuted pool lines guard exhausted 64-bit ids and propagate a
non-exhaustion placement error from a previously validated existing block.
Tests do not mutate private state to force those guards. Fatal death-test paths
also cannot reliably flush child-process coverage counters.

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

Integrate the pool with Context, typed handles and uploads, then implement frame
arenas, completion points and GPU retirement. Native noncoherent
cache-operation validation, failed growth and in-flight replacement/release evidence,
a public runnable resource example, and the maintainer learning handoff remain
required before task review. CPU passes do not establish GPU lifetime safety.
