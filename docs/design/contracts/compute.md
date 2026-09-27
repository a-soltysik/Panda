# Compute and CUDA contract

Accepted specification for native computation and Vulkan/CUDA interoperability.
Windows/Linux device proofs are required before expanding the implementation.

## Responsibility boundary

Panda invokes native computation at a defined point in the frame and coordinates
the declared resources crossing the engine/application boundary. The application
owns algorithm code, private data, kernel/shader selection, internal dependencies,
simulation stepping, and any backend-specific tuning.

Provide two callback forms, not a universal kernel abstraction:

- A Vulkan callback records work into a Panda-provided command buffer, outside a
  rendering scope. It may bind pipelines/descriptors, dispatch, and record its
  internal barriers. It must not end, reset, or submit Panda's command buffer.
- A CUDA callback enqueues work on a Panda-provided stream. If the application uses
  additional streams, it must join their relevant work back into the provided stream
  before Panda's stage-completion marker can be relied upon.

Callbacks can be functions, lambdas, or adapters to ordinary class methods. No
compute base class is required, and computation need not be attached to an entity.
Callbacks do not own the borrowed command buffer or stream. The borrowing
and callback-state lifetime rules are specified below.

## Backend selection and batching

An application may implement an operation in CUDA, Vulkan compute, or both. It
selects the implementation through its configuration at initialization. Enabling
CUDA does not disable Vulkan compute; an ordered chain may use both.

Panda does not translate code, fabricate an unavailable implementation, migrate
state between backends at runtime, or claim the two algorithms are numerically
equivalent. If a requested backend is unavailable, return a clear error/capability
result so the application can decide what to do.

One callback may enqueue many dispatches, kernels, or simulation substeps. Batch
work at the algorithm level; never register one callback per particle. Adjacent
same-backend stages may share an appropriate submission while preserving order.
The first version does not automatically parallelize a dependency graph.

## Resource declarations

Declare the engine-owned buffers crossing each callback boundary and whether that
stage reads, writes, or reads and writes them. Start with whole-buffer declarations.
Do not build a range-based resource graph without a demonstrated requirement.

Private application allocations and dependencies within a callback remain the
application's responsibility. Reflection is not a substitute for declaring access.
The Vulkan declaration below includes stage/access masks sufficient for
boundary dependencies; these are not inferred from shaders.

No silent host copies, per-frame external-memory import, or implicit N-buffer copies
are permitted to make a shared buffer appear safe. Renderer shadow passes count as
consumers just as camera passes do.

## Ordering invariant

For a single shared output buffer, every write must occur after its previous
readers/writers complete, and every consumer must occur after the producer completes.
In particular:

1. Previous Vulkan use completes before CUDA overwrites the buffer.
2. CUDA completes its write before subsequent Vulkan compute or rendering reads it.
3. Rendering includes all uses, including shadows, before the next overwrite.

Use GPU-side external synchronization and Vulkan memory/ownership operations as
required by the selected sharing strategy. Host submission order alone is not
sufficient. Two CPU frames in flight do not make a single shared buffer double-buffered.

Start with a single graphics/compute Vulkan queue, an additional presentation queue
only when required, and one CUDA stream. Do not promise useful overlap between
simulation and rendering that access the same buffer.

## Interoperability

Match the CUDA device to the selected Vulkan physical device by device identity,
not by assuming enumeration index zero is the same GPU. Check the external memory
and semaphore capabilities actually needed for the chosen buffer configuration.

Import external memory once per allocation and keep the mapping valid through its
last use. Correctly handle platform-specific external handle ownership, allocation
size/offset, dedicated-allocation requirements, and CUDA import flags. The
dedicated allocation/export strategy below is separate from ordinary suballocation.

The first version shares buffers, not images, and does not target multi-GPU interop.
Do not silently route unsupported interop through CPU downloads/uploads.

Build-time and runtime availability are distinct:

- CUDA disabled: do not discover or require its compiler, headers, toolkit, or libraries.
- CUDA enabled: expose runtime availability independently from basic rendering.
  A non-NVIDIA machine must not lose rendering merely because an optional CUDA
  component was built. Verify process-start/loading behavior as well as initialization.
- An application explicitly requiring CUDA may fail clearly when it is unavailable.

Use static Runtime linkage as specified in the
[package boundary](../package-and-toolchain.md#c23-and-cuda20-with-one-host-toolchain) and test loading separately;
skipping an initialization function alone does not prove optional runtime support.

## Initialization, pause, replacement, and destruction

Initialize a renderable output before the first draw. A paused stage reuses its last
valid output; it does not require recomputing or reading it back.

Unregistering a stage prevents future callbacks, but does not prove its GPU work
has completed. Callback state, native resources, and imported mappings remain alive
until their last use. Provide an explicit administrative stop-and-wait path for
rebuilding or destroying a stage when needed; it is not part of the normal frame loop.

Replacement uses the resource contract: prepare, initialize/import, publish, retire.
Account for stages that enqueue work but produce no rendered output. A deletion path
that only waits for a graphics frame is insufficient without a demonstrated ordering.

## Failure behavior

Successful callback return means recording/enqueueing succeeded, not that the GPU
has completed successfully. Asynchronous errors may surface later.

If enqueue or submission fails, do not submit consumers that wait forever on a
completion signal which will never be produced. Do not render an output known to be
incomplete. There is no general rollback of already enqueued GPU commands. Define
the stage/frame stop policy, diagnostics, and safe cleanup before releasing the API.

## Callback surface and ownership

Use an ordered list, configured on the context thread outside frame execution.
Registering appends to it; rebuilding the list is an explicit update-phase operation.
There are no priority numbers, inferred dependencies, per-entity callbacks, or worker
threads. Adjacent active CUDA stages form one run; intervening Vulkan work ends it.

Design signatures (not implemented API):

```cpp
using VulkanComputeCallback =
    std::move_only_function<Result<void>(VulkanComputeContext&)>;
using CudaComputeCallback =
    std::move_only_function<Result<void>(CudaComputeContext&)>;

// Vulkan-facing header; no CUDA types in the core.
struct VulkanBufferUse {
    Buffer buffer;
    Access access;                  // Read, Write, ReadWrite
    VkPipelineStageFlags2 stages;   // union of this callback's uses
    VkAccessFlags2 accesses;        // union, consistent with access and usage
};

// CUDA-facing header, in Panda::Cuda only.
struct CudaBufferUse {
    SharedCudaBuffer buffer;
    Access access;
};

// Descriptions own their label, use list and callback.
Context::add_compute(VulkanStageDesc) -> Result<StageRegistration>;
CudaSession::add_compute(CudaStageDesc) -> Result<StageRegistration>;
StageRegistration::set_enabled(bool) -> Result<void>;
StageRegistration::unregister() -> Result<CompletionPoint>;
StageRegistration::stop_and_wait(timeout) -> Result<void>;
```

`StageRegistration` is move-only RAII. Destroying it unregisters future calls but
does not wait. The scheduler and submitted packets retain its callback state as
needed; the last packet release occurs after its completion point. No registration
may capture itself into an owning cycle. A captured raw `this` remains the caller's
lifetime obligation; prefer owned/shared stage state or stop-and-wait before its
destruction. Registration, pause, and unregister inside a running callback are errors.
Retired CUDA callback state goes to the session's administrative retirement queue;
normal frame reclamation must not accidentally invoke an application's blocking
CUDA deallocator. `stop_and_wait`, `collect_retired`, and shutdown drain that queue.
Callbacks must not throw exceptions across this boundary.

`VulkanComputeContext` borrows an already recording command buffer and offers native
views only for declared buffers. `CudaComputeContext` borrows the selected device's
non-default, non-blocking stream and mapped views of declared shared buffers. Both
carry frame sequence/slot metadata, not an inferred simulation timestep. A view is
only valid in that invocation; persistent native pipelines/descriptors may refer to
the same allocation only while retaining its owning handle and respecting replacement.
The stream/command buffer must not be saved, destroyed, reset, or submitted elsewhere.

Callbacks are ordinary host calls, not CUDA stream host callbacks. CUDA launches,
graph launches, and private-stream work are allowed; private streams must join the
provided stream through events before every return, including an error return.
If a backend fault prevents joining private work, report that terminal condition;
Panda cannot reclaim it based only on its own stream. Async host input needs packet-safe
lifetime and must not refer to a returned callback's stack. Application-owned Vulkan pipelines,
descriptors, private images/buffers, CUDA allocations and events stay in retained state.
Resources shared between different callbacks must be declared Panda buffers or have
explicit application synchronization; hidden private cross-callback dependencies
are not inferred. Internal barriers and legal command-buffer state remain the
callback's responsibility. Do not leave an open rendering/query scope on return.

Stage declarations cover whole buffers and all actual boundary uses. Validate
context, creation usage, backend capability, duplicate declarations, and stage/access
compatibility at registration. Storage-compute read/write helpers may fill the common
Vulkan masks, but transfer/indirect/vertex uses remain expressible without a new DSL.
Between Vulkan callbacks and renderer consumers, Panda emits synchronization2 buffer
barriers using accumulated source uses and declared destination scopes. Read/read
needs no data barrier; any write dependency is handled, including prior-frame reads.
The application supplies barriers between operations *inside* a callback.

Core keeps only a private external-stage adapter boundary with native Vulkan handles,
completion values, and enqueue/poll/drain operations. `Panda::Cuda` installs/owns its
implementation and C++23 callback state; only its plain-data Runtime calls cross into
the C++20 adapter. Core never includes CUDA headers or references cudart symbols.
This private seam is not a public backend plugin API or a second compute abstraction.

## External buffer creation

`ContextOptions` can request external-buffer capability as disabled (default),
optional, or required. This is a Vulkan extension request, not a dependency on CUDA.
Before logical-device creation, query and enable the platform's external-memory and
external-semaphore extensions when available. Missing optional support still permits
rendering; missing required support reports a capability error. A session cannot add
extensions retroactively to an existing device.

`CudaSession::create(Context&) -> Result<CudaSession>` matches device UUIDs, selects
the matching CUDA device, imports synchronization objects, and creates one stream.
It is explicit and move-only. One session is allowed per context. Initialization
returns actionable unavailable/unsupported/initialization errors; it does not change
the application's chosen rendering device or silently select a second GPU.

`CudaSession::create_buffer(BufferDesc) -> Result<SharedCudaBuffer>` creates the
Vulkan buffer, export allocation and CUDA mapping transactionally. Its `buffer()`
view returns an owning `Buffer` usable by the renderer or Vulkan callbacks. The
resource state retains the mapping/service independently of the session facade;
closing the session while live shared buffers remain reports `ResourcesInUse`.

For each shared buffer, use one dedicated device-local `VkDeviceMemory`, bind at
offset zero, export exactly the queried opaque platform handle type, and chain
`VkMemoryDedicatedAllocateInfo` for that buffer. Check external-buffer properties
for the actual usage/flags, exportability, compatible handle types, memory type bits,
and dedicated requirements. Import the allocation's requirement size, not merely
the logical data length, with `cudaExternalMemoryDedicated`. Map one non-overlapping
buffer view at offset zero covering the logical buffer range, satisfying alignment.
No pooled exports, aliasing, images, KMT handles, or device groups in version one.

| Platform | Memory export / CUDA import | Semaphore CUDA import | Export handle lifetime |
| --- | --- | --- | --- |
| Linux | Opaque FD / `cudaExternalMemoryHandleTypeOpaqueFd` | `cudaExternalSemaphoreHandleTypeTimelineSemaphoreFd` | Transfer ownership on successful import; close locally on failure |
| Windows | Opaque Win32 NT / `cudaExternalMemoryHandleTypeOpaqueWin32` | `cudaExternalSemaphoreHandleTypeTimelineSemaphoreWin32` | CUDA does not take ownership; close the exported handle after the import attempt |

Use scoped OS-handle guards; apply the same ownership policy to semaphore exports.
One import/mapping lasts for the allocation's lifetime. At safe administrative cleanup,
free the mapped pointer with `cudaFree`, destroy the CUDA external-memory object,
then destroy the Vulkan buffer and free its allocation. Destroy semaphore imports
before their Vulkan exporters, after outstanding signal/wait use is finished.
These are required API lifetime rules, not per-frame operations.
[CUDA import and destruction requirements](https://docs.nvidia.com/cuda/cuda-runtime-api/cuda_runtime_api/group__CUDART__EXTRES__INTEROP.html).

Initialize new shared buffers through Vulkan upload/zero-fill before first external
handoff; initialization is queued on the same Vulkan queue. A generic buffer starts
uninitialized unless initialization was requested; no callback may read undefined
contents. Application simulation initialization can follow in the first CUDA callback
before drawing. Pausing before valid render data exists does not authorize a draw.

Import, mapping, and CUDA teardown may block. Dropping a shared handle only queues
retirement; do not put `cudaFree` in the steady-state frame reclamation loop and
assume it cannot synchronize internally. `CudaSession::collect_retired(timeout)`
is an explicit administrative drain/cleanup, also used by rebuild and shutdown.
Expose pending-retirement bytes so repeated resizing without collection is visible.
Private application resource destructors have the same obligation: a potentially
blocking native free belongs after explicit stop-and-wait, not on the hot path.

## Submission protocol

Use two timeline semaphores, initially zero, each with exactly one signaling backend:

- `G`: signaled only by the graphics/compute Vulkan queue, once per submitted Vulkan
  segment. It is also the context's completion/retirement clock. It is exportable
  only when external capability was enabled; CUDA imports it once.
- `C`: created in Vulkan and imported into CUDA, signaled only on the one CUDA stream,
  once per maximal adjacent CUDA run. Vulkan waits on it. It does not exist with no
  CUDA session. No host signals either timeline to fake completion.

Query external semaphore properties with `VkSemaphoreTypeCreateInfo` specifying
TIMELINE, not just the opaque handle type. Require actual export/import compatibility;
no automatic binary-interoperability fallback.
[Timeline capability query](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceExternalSemaphoreInfo.html).

Allocate monotonically increasing 64-bit values on the context thread. Reserve a
candidate, issue its producer, and publish it only after successful submission/enqueue.
Do not expose unused reserved values as completion points or waits. Respect the
device's outstanding timeline-value limit; on exhaustion stop/drain instead of wrap.
All cross-API signals/waits use conservative ALL_COMMANDS Vulkan stage scope initially.
Narrowing scopes requires evidence; reducing CPU callback count does not require it.

For each CUDA run, let `E` be the union of its declared shared buffers, even if they
are not rendered. Build this union when the schedule changes, not per particle.

| Host issue order | GPU work and committed state |
| --- | --- |
| 1. Submit Vulkan prefix successfully | Earlier Vulkan callbacks/uploads, then release every buffer in E from the graphics family to `VK_QUEUE_FAMILY_EXTERNAL`; signal next G value `g` |
| 2. Enqueue CUDA wait on G=g | Only after step 1 succeeds; no CPU synchronization |
| 3. Invoke adjacent CUDA callbacks | All work uses/joins the one stream; algorithms may batch many kernels |
| 4. Enqueue CUDA signal C=c successfully | Covers the entire run, including private joined work |
| 5. Submit Vulkan continuation | Wait C=c, acquire E from EXTERNAL to graphics family; execute subsequent Vulkan work and signal the next G value |

Release barriers use whole-buffer range, source ALL_COMMANDS / MEMORY_READ|MEMORY_WRITE,
destination NONE / zero. Acquire barriers reverse the queue-family indices, use
source NONE / zero and destination ALL_COMMANDS / MEMORY_READ|MEMORY_WRITE. Within
Vulkan segments use the declared precise scopes. External ownership transfers are
still needed; semaphore visibility is not an exemption from them.
[External ownership and synchronization2](https://docs.vulkan.org/spec/latest/chapters/synchronization.html#synchronization-queue-transfers).

Every CUDA run returns ownership of E to Vulkan before unrelated scheduling proceeds.
The continuation may also be the next CUDA run's release prefix. Even a private-only
run with E empty uses the G/C handoff, and even an otherwise empty final continuation
is submitted: all CUDA work joins the Vulkan completion clock. This intentionally
serializes disjoint stages too; it is the cost of a small, predictable scheduler.

At the end, rendering includes shadows and camera passes and signals G=`frame_done`.
Retain all frame/stage state to that point (conservative retirement is acceptable).
The next frame's release submission follows these commands on the same Vulkan queue;
its release barrier includes earlier reads, and its G signal orders the next CUDA
write after them. Two CPU frame slots never imply two shared allocations.

Example with fresh clocks, one CUDA run per frame:

| Submission sequence | Meaning |
| --- | --- |
| V: initialize/release B, signal G=1 | First Vulkan owner establishes valid contents |
| C: wait G=1, update B, signal C=1 | First producer run |
| V: wait C=1, acquire B, shadows + camera, signal G=2 | Frame slot 0 completes at G=2 |
| V: release B, signal G=3 | Release includes frame 0 readers |
| C: wait G=3, update B, signal C=2 | Second producer cannot overwrite frame 0 input early |
| V: wait C=2, acquire B, shadows + camera, signal G=4 | Frame slot 1 completes at G=4 |

With no CUDA, Vulkan callbacks and rendering normally share one submission plus
barriers. A V/C/V/C/V chain creates two handoff pairs, not a new graph. Adjacent CUDA
callbacks share one pair. A paused/removed stage contributes no callback or fake C
value; existing valid output remains Vulkan-owned and renderable.

CUDA/Vulkan device matching, external semaphore use, and the lack of a CPU copy are
based on [NVIDIA's interop guide](https://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/graphics-interop.html).
The two-clock, single-producer schedule is Panda's design choice, not a claim that
CUDA requires this exact scheduler.

## Lifecycle and faults

Freeze the active schedule for the frame. Unregister returns the last committed G
join point for that stage (zero if unused), not just the last presented frame.
In a faulted context it removes future calls but returns the fault, not a misleading
old completion point that omits partially enqueued work; use the fault drain below.
Stop-and-wait unregisters, waits for that point, and permits release of private state.
Buffer replacement creates/imports/initializes a new resource, then switches stage
declarations and render references together during the update phase. Old submitted
packets keep old resources; their native allocations are never resized in place.
For a simple simulation reset, an explicit stop-and-wait is preferable to a live
state migration mechanism. Pausing only suppresses future work, not pending work.

Preflight rejects invalid requests before acquire/execution where possible. Factory,
registration and replacement-allocation failures are ordinary recoverable errors.
Once callbacks execute, a recording/enqueue/submit failure faults the context and
stops later stages/draws. No automatic retry, rollback, or fallback to old simulation
output is promised. Previously submitted valid frames may finish. Optional CUDA
absence during initialization is different: it does not fault the rendering context.

| Failure point | Action |
| --- | --- |
| Vulkan recording or prefix submit | Discard unsubmitted recordings; do not enqueue CUDA waits for the failed G candidate |
| CUDA wait, callback, or signal enqueue | Do not submit a Vulkan consumer waiting on the missing C signal; retain partially enqueued work and all touched resources |
| Vulkan continuation submit | No completion point for that continuation; retain its CUDA producer and resources for the fault drain |
| Later async CUDA error/device loss | Stop further submission; a previously enqueued signal may never arrive, so normal completion polling is not sufficient |

Keep a small submission ledger: successfully submitted G values, enqueued C values,
whether a CUDA tail remains unjoined, and acquired/presented image state. This is
failure bookkeeping, not an execution graph. On a fault, poll the last known Vulkan
work and the provided CUDA stream (including any unjoined tail) with error checks and
a finite deadline. Normal slot waits likewise periodically check backend errors.
Do not call an unbounded device/stream synchronize to diagnose a missing signal.
Never advance a semaphore manually to force dependent reads of incomplete data.

If known work drains on a healthy device, release unused acquired images, then clean
up imports/native resources in dependency order. If completion cannot be established,
return a terminal error and quarantine the affected state; do not free memory that
may still be in use. Default checked destruction terminates with diagnostics rather
than continuing with corrupted state. Version one does not promise in-process GPU
hang recovery, and a broken driver call itself cannot be made bounded by this API.

Healthy shutdown: stop registrations and new frames, drain G (which includes joined
CUDA work), drain any recorded unjoined tail, finish presentation lifetimes separately,
release scene/callback/resource owners, collect retired mappings, destroy CUDA stream/
semaphore imports, then Vulkan resources/device. Do not reset the process-wide CUDA
device or destroy CUDA resources owned by the application.

Use the specified static cudart linkage and explicit initialization; inspect actual process
dependencies and launch a CUDA-enabled sample without an NVIDIA driver on both OSes.
Probe this before publishing optional runtime support. A failure reopens the loading
decision; it does not authorize adding a loader/plugin framework or dropping AMD/Intel.

## Required proof before engine expansion

Use a small synthetic producer, not a full simulation solver, to demonstrate:

- Vulkan-only updates and CUDA-only updates of an instanced buffer.
- Mixed Vulkan/CUDA/Vulkan order and protection against the next frame's overwrite.
- Batched dispatches, pause/resume, output initialization, unregister, replacement,
  and shutdown with work in flight.
- Explicit unsupported-device/backend outcomes and rendering without CUDA.
- Submission/enqueue failure handling without a stranded semaphore wait.
- No CPU bounce, per-frame import, per-particle CPU work, or steady-state global idle.

Required device checks:

| Probe | Observable result and stop condition |
| --- | --- |
| External buffer import | Windows/Linux capability + UUID + dedicated export/import/mapping; stop if the selected handle/timeline combination is unsupported |
| Ordered GPU access | Two-frame tagged buffer, V/C/V/C/V and shadow/camera readers; GPU-side generation/checksum checks detect early overwrite; stop on mismatch or validation error |
| Retirement and replacement | Disable/unregister, rebuild, last-handle release, and private-only CUDA run; state destructors occur only after their recorded join/drain |
| Submission failures | Inject failures before each host issue step; no wait refers to an unissued producer; delayed async faults run in a supervised test process with a finite timeout |
| Unavailable CUDA runtime | CUDA-enabled binary starts and renders with unavailable driver/device on Windows and Linux; an explicitly required CUDA request returns a useful error |
| Window lifecycle | Maintenance1 support and normal/error-path present fence, acquired-image release, resize/minimize/shutdown checks on both OSes |

CPU protocol tests use a fake native-call log to check producer-before-wait and state
transitions; they do not replace the GPU proof. GPU tests use a tiny synthetic producer,
not a solver. Test-only readbacks/checksums are allowed to verify results; they are not
part of the normal renderer path. Record native calls/import counts, Nsight Systems
ordering, and unprofiled timings, including administrative teardown cost.

Validate both operating systems. Use a system timeline to inspect ordering and
sanitizers/validation for complementary checks; no single tool proves cross-API safety.

Reference: [CUDA graphics interoperability](https://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/graphics-interop.html).
