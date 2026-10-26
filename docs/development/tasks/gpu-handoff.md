# Vulkan and external backend handoff

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Design and implement an explicit GPU-side handoff for a Panda-owned external
buffer, starting with `Panda::Cuda`. Preserve the generic external-memory boundary
in Panda Core. The application chooses when to submit compute work; this task does
not add a persistent callback registry, frame scheduler or general task graph.

Non-goal: No CPU copies, routine global waits, backend translation, or simulation
step policy inside Panda.

## Implementation direction

Use one exportable Vulkan timeline semaphore per external buffer, with an opaque
FD on Linux or opaque Win32 handle on Windows. Core submits a buffer ownership
release to `VK_QUEUE_FAMILY_EXTERNAL` and signals the semaphore. `Panda::Cuda`
imports the paired semaphore with the buffer and waits on its session stream before
accessing the data, then signals the next timeline value when its work is done. A
later Core acquire waits for that value and returns ownership to Vulkan. Normal
handoffs use GPU queue/stream ordering without CPU waits or copies. Query semaphore
exportability on the selected device and report `Unsupported` when absent. Core
exposes generic external-buffer release/acquire operations and the paired platform
semaphore handle; it does not expose CUDA types. External-buffer creation may still
succeed without semaphore support, but a requested cross-API handoff must return
`Unsupported`.

Keep the stream and mapped pointer private in this task. The following compute
submission task owns the application-facing native extension point, where it can be
introduced with a usable work-submission path instead of as a separate low-level API.
Core remains free of CUDA types.

The current implementation slice queries export support for timeline semaphores,
adds the optional paired handle to Core's external-buffer owner, and imports and
retains it with the allocation in `Panda::Cuda`. It does not yet submit Vulkan
ownership barriers or enqueue CUDA wait/signal operations; ordered GPU handoff and
GPU proof remain required for acceptance.

Paths: `include/core/panda/`, `include/cuda/panda/cuda/`, `src/core/vulkan/`,
`src/cuda/`, tests, and directly affected documentation.
Specifications: [Compute and external buffers](../../design/contracts/compute.md),
[resource ownership](../../design/contracts/resources.md#lifetime-is-not-synchronization).

## Acceptance

Prove two consecutive producer-to-consumer handoffs on Windows and Linux with
tagged GPU data, Vulkan synchronization validation and CUDA ordering inspection.
Cover Vulkan-to-CUDA and CUDA-to-Vulkan access, failure before each submitted wait,
resource retention during use, and cleanup. Record that no steady-state CPU copy or
device/queue-wide wait is needed.

## Current verification

- Windows: MSVC 14.51.36231; `cmake --build --preset msvc-fast --parallel` and
  `ctest --preset msvc-fast` passed, 442 tests with zero failures and four skips
  (one benchmark-output case and three noncoherent-memory cases). The system test
  exercised semaphore-import failure cleanup against a Vulkan-exported buffer and
  a mocked CUDA runtime.
- WSL Ubuntu: `cmake --preset gcc-fast`,
  `cmake --build --preset gcc-fast --parallel`, and `ctest --preset gcc-fast`
  passed, 442 tests with zero failures and five skips (CUDA external-buffer
  integration found the selected device/handle unsupported; the semaphore-import
  failure case found no timeline-semaphore export support; three noncoherent-memory
  cases).
- CUDA adapter tests verify opaque FD/Win32 handles use CUDA's timeline-semaphore
  import types. Context lifecycle tests verify the Vulkan capability query names
  timeline semaphores and rejects binary-only export support.
- Cross-API producer/consumer ordering, Vulkan ownership barriers, CUDA semaphore
  waits/signals and tagged-data GPU proof remain unimplemented. The WSL hardware
  scenario was skipped; Windows exercised the mocked import-failure path, not CUDA
  semaphore import on hardware.

Learning: Trace a buffer's previous Vulkan use, CUDA producer and next Vulkan read;
explain why a shared OS handle and a live C++ owner are not synchronization.
