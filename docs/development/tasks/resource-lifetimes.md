# Allocation, uploads and resource lifetimes

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Implement typed handles, limited pooled allocator, upload batches, frame-safe parameter storage and completion-based retirement.

Non-goal: No CUDA export or per-object device idle.

Interface: Buffer/Texture/Mesh handles, UploadBatch and context-scoped CompletionPoint.

Paths: `../../../include/core/panda`, `src/core/vulkan/resources/`, `tests/cases/unit/`, `tests/gpu/` and directly affected documentation.
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
subtraction before addition to avoid 64-bit overflow. This is CPU bookkeeping, not
GPU allocation or retirement; the future block owner must delay release until use
completes. Metadata uses standard containers and is not a frame arena.

The executable `panda_unit_range_allocator` demonstrates split/alignment, all six
release orders for three adjacent ranges, fragmented exhaustion, invalid requests
and releases, and 64-bit size/alignment boundaries. For example, allocating three
bytes then eight bytes aligned to sixteen leaves a reusable thirteen-byte gap;
alignment padding does not belong to the second allocation.

## Verification

CPU range slice, with CUDA disabled:

- Windows: MSVC 19.51.36260, clang-tidy 22.1.8 and cppcheck 2.19.
  `cmake --preset msvc-quality`, then
  `cmake --build --preset msvc-quality --target panda_unit_range_allocator --parallel 2`
  passed with warnings-as-errors and both analyzers.
  `ctest --preset msvc-quality -R RangeAllocator` passed 11/11;
  `ctest --preset msvc-quality -R '^(panda_core_format|panda_unit_range_allocator_format)$'`
  passed 2/2.
- WSL Ubuntu: GCC 16.0.1, clang-tidy 22.1.2 and cppcheck 2.19.
  `scripts/build-wsl.ps1 -Preset gcc-quality -Target panda_unit_range_allocator`
  passed with warnings-as-errors and both analyzers; CTest `-R RangeAllocator`
  in the managed mirror's quality build passed 11/11.
  `scripts/build-wsl.ps1 -Preset gcc-sanitizers -RunTests -TestRegex RangeAllocator`
  passed 11/11 with ASan/UBSan and no findings.
- `py -3 .agents/skills/panda-project-maintenance/scripts/check_docs.py` and
  `git diff --check` passed. Direct clang-format checks covered the new files.

Only the range suite and relevant formatting cases were run; full regression,
Windows ASan, GPU validation and device behavior were not checked for this slice.

## Remaining work

Implement Vulkan memory-type pools and dedicated fallback, mapped/noncoherent
storage, typed handles and uploads, frame arenas, completion points and retirement.
GPU alignment/flush, failed growth and in-flight replacement/release evidence,
a public runnable resource example, and the maintainer learning handoff remain
required before task review. CPU passes do not establish GPU lifetime safety.
