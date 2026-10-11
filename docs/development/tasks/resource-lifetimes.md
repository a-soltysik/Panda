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

`MemoryBlock` owns one ordinary, non-exportable `VkDeviceMemory` allocation with
Vulkan-Hpp RAII. Creation checks the memory type, selected heap and maximum
allocation size, and rejects protected/lazy memory. Host-visible blocks are mapped
once for their lifetime. Noncoherent slices reserve whole atoms and use alignment
at least as strict as both the resource and atom requirements. Explicit
flush/invalidate validates a live slice and rounds the affected bytes within it;
coherent memory still validates requests but needs no cache call. The mapped view
is internal and does not authorize access while the GPU uses the range.

Failed allocation never frees an undefined output; failed mapping frees the valid
allocation without unmapping. Destruction rejects outstanding ranges, then unmaps
before freeing memory, without a device-wide wait. Resource owners must complete
GPU use and destroy bound resources before releasing their ranges. Pool ownership,
the device-wide allocation-count budget and deferred retirement remain to be added.

The executable `panda_unit_range_allocator` demonstrates split/alignment, all six
release orders for three adjacent ranges, fragmented exhaustion, invalid requests
and releases, and 64-bit size/alignment boundaries. For example, allocating three
bytes then eight bytes aligned to sixteen leaves a reusable thirteen-byte gap;
alignment padding does not belong to the second allocation.

The executable `panda_system_memory_allocation` binds two native buffers to
separate slices in one block and checks mapped access with Vulkan validation.
Coherent and noncoherent variants report unavailable memory types as skips.
It submits no GPU work; upload visibility and in-flight retirement are not tested
by this scenario.

## Verification

Range and ordinary-memory-block slice, with CUDA disabled. Windows used MSVC
19.51.36260, Vulkan SDK 1.4.341.1, clang-tidy 22.1.8 and cppcheck 2.19. WSL Ubuntu
used GCC 16.0.1, Vulkan 1.4.341, clang-tidy 22.1.2 and cppcheck 2.19.

- Windows: `cmake --preset msvc-quality`, then
  `cmake --build --preset msvc-quality --target panda_unit_memory_block panda_system_memory_allocation panda_unit_range_allocator --parallel 2`
  passed with warnings-as-errors, clang-tidy and cppcheck.
  `ctest --preset msvc-quality -R 'MemoryBlock|RangeAllocator|MemoryAllocationSystem' --verbose`
  passed 25 unit cases and the coherent system case on NVIDIA GeForce RTX 4070 Ti
  (memory type 2), with no Vulkan validation errors. Noncoherent was skipped:
  no compatible memory type. The five relevant CTest formatting cases passed.
  A full `msvc-tests` build and `ctest --preset msvc-tests -LE system` passed 116/116.
- WSL: `scripts/build-wsl.ps1 -Preset gcc-quality -Target panda_unit_memory_block,panda_system_memory_allocation,panda_unit_range_allocator`
  passed with the same quality checks. CTest in the managed mirror with
  `-R 'MemoryBlock|RangeAllocator|MemoryAllocationSystem' --verbose` passed 25 unit
  cases and the coherent system case with no Vulkan validation errors, using
  llvmpipe (LLVM 21.1.8, memory type 0). Noncoherent was skipped: no compatible type.
  `scripts/build-wsl.ps1 -Preset gcc-sanitizers -RunTests` passed all 116
  noninteractive cases under ASan/UBSan with no findings.
- `py -3 .agents/skills/panda-project-maintenance/scripts/check_docs.py` and
  `git diff --check` passed.

Native noncoherent flush/invalidate and physical-GPU Linux behavior remain
unverified. Controlled Vulkan tests cover atom isolation/rounding, allocation/map
failure cleanup, cache-operation failures and live-range destruction rejection.
Windows ASan and synchronization validation were not run for this slice; the
system scenario submits no GPU work and does not prove upload or retirement safety.

## Remaining work

Implement Vulkan memory-type pools and dedicated fallback, typed handles and
uploads, frame arenas, completion points and retirement. Native noncoherent
cache-operation validation, failed growth and in-flight replacement/release evidence,
a public runnable resource example, and the maintainer learning handoff remain
required before task review. CPU passes do not establish GPU lifetime safety.
