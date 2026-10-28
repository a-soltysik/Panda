# Vulkan and external backend handoff

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Implement and prove an explicit GPU-side handoff for a Panda-owned external buffer,
starting with `Panda::Cuda`. Preserve the generic external-memory boundary in Panda
Core. The application chooses when to submit compute work; this task adds one
typed callback for a single buffer handoff, not a persistent callback registry,
frame scheduler or general task graph.

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

Core remains free of CUDA types. `SharedCudaBuffer::submitWork` is the application
extension point for one handoff. It queues a wait for the Vulkan release value on
the session stream, invokes the callback with that stream and the borrowed mapped
device address, and queues the next timeline signal on the same stream. The callback
returns its enqueue result; Panda still queues the signal after a reported callback
error so the application can queue Vulkan reacquisition before handling that error.
The method returning means these operations were enqueued, not that GPU work
completed. It accepts only the next expected release value, and calls on a shared
buffer must be serialized. Its result separates callback enqueue failure from
wait/signal failure; a failed signal faults that imported handoff and prevents
further submissions on it. Public handoff methods pass `ExternalTimelineValue`;
conversion to the integer representation happens at the native API boundary.

`ExternalBufferManager` creates buffers and queues generic release/acquire calls.
Core records Vulkan buffer ownership barriers and signals or waits on the paired
timeline semaphore, retaining submitted commands and resources until completion.
`Panda::Cuda` imports the buffer and semaphore, then `SharedCudaBuffer::submitWork`
orders application work on the matched session stream. Destroying the final shared
owner synchronizes that stream before releasing imported resources. Tagged-data
producer/consumer ordering and GPU proof remain required for acceptance.

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

- Windows: MSVC 14.51.36231. `msvc-quality`, public-header isolation and format
  checks passed. Parallel links intermittently failed with `LNK1163` in untouched
  test objects; complete quality and fast builds passed when rerun with one job.
  `msvc-fast` passed all 455 tests with four skips (one benchmark-output case and
  three unavailable noncoherent-memory cases). Quality reports six
  unchecked-container warnings in the untouched
  `tests/cases/unit/core/UploadBatchImpl.cpp`. The system tests exercised Vulkan
  external-buffer release/acquire with mocked CUDA calls.
- WSL Ubuntu: `gcc-quality`, public-header isolation and format checks passed.
  `gcc-fast` built and all 455 tests passed (seven skips: four CUDA/external-semaphore
  cases and three unavailable noncoherent-memory cases). Linux API
  documentation generation passed. Quality reports the same six unchecked-
  container warnings in `UploadBatchImpl.cpp`.
- Windows `clang-cuda-analysis-windows` and WSL `clang-cuda-analysis` passed for
  host and device code. Both report that the installed CUDA version is newer than
  Clang's latest partially supported version 12.9.
- `python .agents/skills/panda-project-maintenance/scripts/check_docs.py` checked
  76 Markdown files and the task queue with no structural errors. Windows
  `docs-checks` checked 41 diagrams; all were valid.
- Coverage was not rerun for this iteration. The current test runs do not prove
  tagged Vulkan/CUDA data ordering or hardware execution of CUDA wait/signal.
- Tagged-data producer/consumer ordering, synchronization-validation coverage of
  the cross-API path, and GPU-side CUDA wait/signal proof remain incomplete.

Learning: Trace a buffer's previous Vulkan use, CUDA producer and next Vulkan read;
explain why a shared OS handle and a live C++ owner are not synchronization.
