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

The current implementation has an `ExternalBufferManager` for creation and generic
release/acquire calls. Core records Vulkan buffer ownership barriers and signals or
waits on the paired timeline semaphore, retaining submitted commands and resources
until completion. `Panda::Cuda` has private timeline wait/signal operations on its
session stream and synchronizes that stream before destroying imported resources.
The current public API has no compute-work path that invokes those operations around
application work, so ordered cross-API use and GPU proof remain required for acceptance.

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
  `ctest --preset msvc-fast` passed, 450 tests with zero failures and four skips
  (one benchmark-output case and three unavailable noncoherent-memory cases).
  External-buffer creation, Vulkan timeline release and mocked CUDA import-failure
  cleanup ran on the device.
- WSL Ubuntu: `cmake --build --preset gcc-fast --parallel` and
  `ctest --preset gcc-fast` passed, 450 tests with zero failures and six skips
  (CUDA external-handle support unavailable, timeline-semaphore export unavailable
  for two system cases, and three unavailable noncoherent-memory cases).
- The GCC coverage run passed all 412 CPU unit and integration tests (267 unit,
  145 integration). `panda_coverage_report` measured 87.8% lines, 91.5% functions
  and 71.4% branches overall; `ExternalContext.cpp` measured 85.2% lines and 64.3%
  branches. The local `panda-bench` smoke could not run because `xvfb-run` is
  unavailable, so the coverage report does not include the CI software-Vulkan
  frame path.
- `python .agents/skills/panda-project-maintenance/scripts/check_docs.py` checked
  75 Markdown files and the task queue with no structural errors. Codecov now
  receives coverage uploads on pull requests as well as pushes to `master`; a
  hosted PR comment has not yet been verified.
- Tagged-data producer/consumer ordering, synchronization-validation coverage of
  the cross-API path, and GPU-side CUDA wait/signal proof remain incomplete.

Learning: Trace a buffer's previous Vulkan use, CUDA producer and next Vulkan read;
explain why a shared OS handle and a live C++ owner are not synchronization.
