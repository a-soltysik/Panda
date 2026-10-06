# Resource and frame contract

Accepted specification for ownership, allocation, frames and failure handling.
Implementation and device evidence follow [the plan](../../development/PLAN.md).

## Ownership

- A context owns the Vulkan device and the services required to retire GPU resources.
  It outlives scenes, resource handles, and optional modules using that context.
- A scene owns its entities and components. An entity reference contains scene
  identity and a generation-aware identifier; copying it does not keep an entity alive.
- Meshes, textures, and materials use shareable owning value handles. Standard
  reference counting is acceptable; a bespoke smart-pointer framework is not a goal.
- Frame preparation/submission retains the resources it needs until their final GPU
  use completes. Removing an entity changes future frames, not already submitted work.
- Do not expose long-lived raw pointers into relocatable component storage.

The normal application owner establishes a clear shutdown order. It must not be
necessary for ordinary callers to manually destroy every Vulkan object in dependency
order. An explicit context shutdown may wait for outstanding work; per-object
destruction must not turn into a global device idle.

## Lifetime is not synchronization

A shared handle prevents premature destruction. It does not make concurrent writes
safe. Renderer-owned camera, transform, light, and material parameter data must use
frame-safe storage or an equivalent proven update policy.

An application-owned shared compute buffer is not silently copied for each frame.
Its read/write ordering is declared through the [compute contract](compute.md).
CPU updates and uploads also require an explicit staging/visibility policy; mapping
memory does not by itself permit overwriting data still being consumed by the GPU.

## Frame contexts and retirement

The initial design has two frame contexts. Before reusing one, wait only for the
completion that protects that context's command and transient resources. Frame
context count is not assumed to equal swapchain image count.

Resource destruction may enqueue deferred retirement. Reclaim an allocation only
after all relevant Vulkan and CUDA uses are complete, including a compute stage
whose output is not rendered. Track actual use, not only the last presentation.

Avoid routine `vkDeviceWaitIdle`, `vkQueueWaitIdle`, or CUDA stream/device-wide CPU
waits in rendering and interop. Administrative operations may block when documented.
The completion model is specified below and in the compute contract.

## Mutation and replacement

The first version supports main-thread scene mutation during the documented update
phase. It does not promise thread-safe mutation of arbitrary scene data or a parallel
ECS scheduler.

Resize/reallocation is transactional at the application-visible boundary:

1. Validate the request and prepare replacement resources.
2. Initialize them and establish any external mappings required for use.
3. Publish the replacement at a safe point.
4. Retire old resources after their last use.

Failure before publication keeps the previous valid state. This is not a promise
that GPU work already submitted can be rolled back. Swapchain recreation has a
separate failure rule below; it cannot promise that an obsolete surface remains usable.

Shared material changes affect all users. Applications clone a material explicitly
when private parameters are needed; per-instance tint is not a material clone.

## Errors

Use `std::expected<T, Error>` for recoverable operation failure. Use `std::optional`
when absence is an ordinary result, not to hide a diagnostic. Fallible construction
uses factories; recoverable result types are `[[nodiscard]]` where appropriate.

Use one `Error` and `Result<T>` model across Panda components. `ErrorCode` groups
failures by a useful caller response rather than by native operation: correct an
`InvalidArgument`, change a request or environment for `Unsupported`, release
resources before retrying `ResourceExhausted`, adjust or retry a `Timeout`, recreate
a lost `Surface`, or stop using/rebuild a lost `Device`. `BackendFailure` means the
engine has no general recovery action; inspect its message and diagnostics.
`Error` owns its message, carries an optional `{api, code}` native status, and stores
the `std::source_location` where it was formed. It does not need a separate operation
string or per-operation enum. Callers that only need simple handling can check
`if (!result)` and display `result.error().message`; callers that can recover can
inspect `Error::code`, and detailed logs can include `native` and `source`.
The Vulkan adapter explicitly classifies statuses with known recovery actions;
unknown native status defaults to `BackendFailure` while preserving its raw code.
The Vulkan result switch is exhaustive under `-Wswitch-enum`, so adding a distinct
SDK result requires a deliberate recovery-category decision.

Do not throw exceptions for Panda error propagation. Catch exceptions from
dependencies at a meaningful adapter boundary and convert them once into the public
error model.

Do not wrap every `std::vector::push_back` or allocation in a catch block. Catastrophic
allocation failure and broken fundamental invariants may terminate with a useful
diagnostic. An explicit checked fatal/expect operation is allowed and must remain
active in release builds; unchecked access to an empty result is not that operation.

Text diagnostics display a best-effort short function name: the last two qualified
components, without ordinary template arguments or parameters. Lambdas display as
`<lambda>` and operators retain their operator spelling. Shortening uses a fixed
128-byte stack buffer without extra allocation; unusually complex signatures can
be approximate or truncated. File and line still identify the call site, and
the original `std::source_location` remains available to logger sinks. Text sinks
and fatal diagnostics display paths inside Panda relative to its source root,
using a non-owning view without filesystem queries or allocation. Paths outside
that root retain their original spelling.

Fatal diagnostics first write and flush their allocation-free context directly to
stderr, independently of logger filters, sinks and locks. With
`PANDA_ENABLE_STACKTRACE=ON` (the default), they then attempt a `std::stacktrace`
limited to 32 frames before aborting, retaining diagnostic helper frames rather than
skipping a fixed prefix. Trace capture/symbolization may allocate;
failure or an empty trace does not prevent abort. Optimized builds may omit frames,
and source/function detail depends on available debug symbols.

Ordinary logging does not capture stacks. Call `log::writeWithStacktrace` explicitly
with a severity, checked format text and arguments (optionally an explicit Logger).
Collection occurs only after filtering. Failure preserves the original message;
other normal logging/formatting allocation failures can still propagate. The trace
belongs to the current thread at the logging call; C++23 does not recover an
exception's original throw stack. Disabling stack traces keeps these APIs usable
and emits the ordinary message without a trace.

Log sink write and flush operations are nonthrowing and return
`std::expected<void, log::SinkError>`. Stream adapters contain formatting and stream
exceptions at this boundary. The logger reports sink failure directly to stderr
and continues delivery to other sinks. Message formatting before delivery retains
the ordinary allocation-failure policy.

Do not apply a blanket no-exceptions compiler flag if it prevents the dependency
adapters from catching exceptions. Keep Vulkan-Hpp's nonthrowing configuration
consistent across translation units; exact macros depend on the pinned version.

Asynchronous GPU errors can be discovered after a successful enqueue. No general
device-loss recovery or rollback system is required in the first version. Failure
must still avoid an infinite wait on work that was never submitted or signaled.

## Handles and API boundary

The following are design signatures; implemented declarations are documented in
their public headers. Remaining entries describe planned resource APIs.

| Type/operation | Semantics |
| --- | --- |
| `Context::create(ContextOptions) -> Result<Context>` | Move-only owner with stable, heap-owned internal state; operations on one context are serialized |
| `Context::createWithSurface(WindowSurface&, ContextOptions) -> Result<Context>` | Creates a windowed context; the borrowed window surface and native window outlive the context |
| `Buffer`, `Texture`, `Mesh`, `Material` | Copyable typed handles around shared resource state; empty handles are detectable and rejected where a resource is required |
| `Context::create_buffer(BufferDesc) -> Result<Buffer>` | Fixed byte size and creation usage; never implicitly resize an allocation behind a borrowed native handle |
| `UploadBatch::write(Buffer, offset, span<const byte>) -> Result<void>` | Copies the caller's bytes into owned staging before returning; validates size, alignment and usage |
| `UploadBatch::submit() -> Result<CompletionPoint>` | Submits on the same graphics/compute queue; no CPU wait for completion |
| `Context::poll(CompletionPoint) -> Result<bool>` | Reports GPU completion or failure; not a presentation/display timestamp |
| `Context::wait(CompletionPoint, timeout) -> Result<WaitStatus>` | Explicit administrative wait; timeout is distinct from completion and backend failure |
| `Context::close(timeout) -> Result<void>` | Stops new work, drains known submissions/presentation, then closes only when dependent owners are gone |

`ContextOptions::enableValidation` enables the installed Khronos validation layer and
registers a `VK_EXT_debug_utils` callback that routes diagnostics to the Panda logger.
It does not depend on Vulkan-layer environment variables. Requesting validation when
the layer or debug-utils extension is unavailable returns an `Unsupported` error.

`Result<T>` means `std::expected<T, Error>`, not another result framework.
`CompletionPoint` is an opaque context identity plus committed Vulkan timeline
value, comparable only within that context. Value zero means no submitted work.
No public API lets a caller signal it, fabricate future values, or use it after
context destruction. `Error` has an action category, owned message, optional native
backend status and source location; it does not retain references to temporary strings.

Keep `BufferDesc` small: byte size, usage flags, memory intent, external-sharing
intent, initialization policy, and debug label. The ordinary native extension uses
Vulkan usage flags rather than inventing a complete mirror of Vulkan. Meshes and
textures provide higher-level factories. No public general-purpose memory mapper
is needed initially: uploads handle CPU writes; a test/readback helper can explicitly
wait, invalidate, and copy results. CUDA-shared buffers are never CPU-mapped.

`panda/NativeVulkan.hpp` exposes borrowed device/physical-device handles, enabled
feature information and buffer views for native pipeline/descriptor setup. It
does not expose Panda's queue submission or semaphore ownership. Native handles
remain tied to their owning Context/resource; recording accesses to Panda buffers
must still happen through a declared callback. Descriptor creation is not GPU use.
The application must not destroy borrowed objects or externally submit their use.

Context identity is checked when combining handles. Handles do not extend context
lifetime; `close` reports `ResourcesInUse` if external scene/module/resource owners
remain after internal in-flight retention is drained. It stays in Closing so those
owners can be released and close retried. A destructor invokes the checked close
path; an unresolved ownership violation or unsafe teardown is fatal with diagnostics,
not use-after-free or an unbounded silent wait. Shutdown timeout is configurable,
initially 10 seconds; a slow application may explicitly increase it.

Use standard `shared_ptr` for resource state, not a public smart-pointer hierarchy.
Capture strong handles and callback state in each submitted work packet. The last
CPU release schedules native destruction on the current context execution thread, never calls
device idle. Reclamation requires both no owners and completion of all submitted
uses. Reference counting does not authorize concurrent resource mutation; version one
requires creation, mutation, native access, and final release on the serialized
context execution thread. A quiescent context may move to another thread; windowed
operations still follow the window provider's thread requirements (GLFW uses the
main thread).

## Allocation, uploads, and descriptors

Use an internal allocator with an address-ordered free list, aligned first-fit
placement, and adjacent-free-range coalescing. No relocation, aliasing, sparse
resources, defragmenter, residency manager, or allocator plugin API in version one.

- Separate pools by Vulkan memory type and by buffers versus optimal-tiled images.
  Do not support linear-tiled images initially; this avoids mixed-resource granularity
  bookkeeping without ignoring Vulkan alignment requirements.
- Lazily allocate 64 MiB device-local blocks and 8 MiB host-visible blocks, capped
  against the selected heap size. Requests larger than half a block use a dedicated
  allocation. Honor required or preferred dedicated allocations reported by Vulkan.
  If a new pool block cannot be allocated, try one exact-size dedicated allocation;
  then return the allocation error. No eviction or repeated allocation guessing.
- Keep allocation count and committed/live/pending-retirement bytes visible in
  diagnostics. Check reported limits and integer/alignment overflow. Retain at most
  one empty ordinary block per pool; release other empty blocks after retirement.
- Persistently map host-visible blocks. Prefer coherent memory, but support explicit
  flush/invalidate. Separate independently reusable slices on non-coherent atom
  boundaries and keep rounded operations inside the mapped allocation.
- Use frame-local linear arenas for camera/light/material/transform data and staging;
  reset only after that frame slot completes. Reuse high-water capacities instead
  of allocating every frame. Large explicit upload batches retain their own staging
  pages to their submission point. Overflow grows pages, not an unsafe wraparound.
- Exportable CUDA buffers use dedicated allocations, outside ordinary pools; the
  [compute contract](compute.md#external-buffer-creation) owns the rules.

All uploads use the graphics/compute queue. Track pending upload uses and issue
transfer-to-consumer dependencies before first use. Updating an existing buffer
requires ordering after its earlier consumers too; queue order alone is not a memory
barrier. Caller data may be released after `write`, staging only after completion.
Renderer parameter edits update CPU state, then copy a snapshot into the next safe
frame slot. A host-visible allocation is not permission to modify an in-flight slot.

Renderer-owned depth, MSAA, offscreen color and shadow attachments belong to frame
slots and are reused only after slot completion. Swapchain images belong to their
swapchain generation. Published sampled textures are immutable; edits publish a
replacement handle. Track layout and source stage/access per image mip/layer range,
including upload, mip generation, attachment writes, sampling and presentation.
Only new/discarded contents may transition from UNDEFINED; ownership/lifetime is
not a substitute for image barriers. No general transient-image aliasing graph is
needed. The [rendering specification](rendering.md) defines formats and attachment use.

Start with descriptor pools owned by frame slots, without update-after-bind or
bindless descriptors. Cache identical binding sets within a frame; reset/repopulate
after slot completion, growing pools by chunks when capacity is insufficient.
Descriptors do not own their buffers/images: the frame packet separately retains
them. Material edits and shader replacement never rewrite an in-flight set.
Persistent descriptors used inside an application callback belong to its retained
state; their mutation and last-use rules remain the application's responsibility.
Measure descriptor CPU cost before introducing persistent engine descriptor caches.

Normative constraints: [Vulkan memory allocation](https://docs.vulkan.org/spec/latest/chapters/memory.html),
[memory requirements](https://docs.vulkan.org/refpages/latest/refpages/source/VkMemoryRequirements.html),
[dedicated requirements](https://docs.vulkan.org/refpages/latest/refpages/source/VkMemoryDedicatedRequirements.html),
[descriptor pools](https://docs.vulkan.org/refpages/latest/refpages/source/VkDescriptorPoolCreateInfo.html).
The pool algorithm and sizes above are Panda design choices, not Vulkan prescriptions.

## Frame and window state

Two frame slots each own command pools/buffers, descriptor pools, parameter/staging
arenas, timestamp ranges, and retained work packets. Their reuse point is the final
`G` value defined in the [compute protocol](compute.md#submission-protocol).
Even a CUDA stage with no rendered output is joined before this value. Rendering
without CUDA uses the same `G` completion path without any CUDA objects/imports.

For a normal frame: reclaim completed work, wait/poll the selected slot, acquire an
image, prepare/execute the ordered work, submit the final rendering segment, then
present. Do not invoke compute callbacks on an unsuccessful acquire. CPU scene
updates may continue while suspended; the application owns simulation time and
must not accumulate hidden catch-up steps in Panda. Independent compute submission
without a renderable window is deferred, not implicitly promised by callbacks.

Windowed rendering requires the following additional feature:
`VK_KHR_swapchain_maintenance1`, or its equivalent EXT variant, including matching
surface-extension dependencies and the enabled feature bit. Prefer KHR when available.
Vulkan 1.3 alone does not guarantee this capability. Unsupported window/device
combinations fail clearly.
Do not silently implement an older-driver fallback with unproved teardown behavior.
Core creation also explicitly queries/enables dynamic rendering, synchronization2
and timeline semaphores; a Vulkan version number alone is not the feature check.

Use binary acquire semaphores per frame slot and render-finished semaphores per
swapchain image. Track a presentation fence for each outstanding present and wait
for its reuse/destruction, independently of `G`. Prefer one graphics/present family;
if distinct, use concurrent sharing for swapchain images only. Ordinary resources
remain exclusive. Present fences protect resource recycling, not exact display time.

| State/event | Required behavior |
| --- | --- |
| Ready | Accept updates, uploads, and frames |
| Zero framebuffer extent | Suspended: no acquire, frame callbacks, or zero-sized attachments; keep processing events |
| Acquire timeout/not-ready | Return a skipped-frame result; no fictitious completion point or consumed frame slot |
| Acquire out-of-date | RecreatePending; no acquired image or callback work assumed |
| Acquire suboptimal | Finish the acquired frame, then request recreation |
| Present out-of-date/suboptimal | Preserve the already submitted completion point and presentation record; recreate at the safe boundary |
| Surface lost | Stop window work and report the error; automatic platform-surface recovery is not a version-one requirement |
| Callback/enqueue/submit failure | Follow the compute fault policy; do not continue drawing uncertain output |
| Device loss | Faulted: stop all new GPU work; no transparent device reconstruction |
| Closing/Closed | Reject new work; bounded drain and owner checks, then release native services |

At recreation, drain the relevant `G` points and outstanding presentation fences;
administrative waiting is allowed. Build replacement attachments and swapchain,
then publish a complete usable generation. Calling `vkCreateSwapchainKHR` with an
old swapchain can retire the old chain even when replacement fails: retain owned
resources for cleanup, but stay RecreatePending/error rather than promising rollback
to that chain. Do not apply the ordinary buffer replacement guarantee to WSI.

If a frame aborts after acquire but before its normal submission, consume the
acquire semaphore in a cleanup submission when the device is healthy, wait for
that submitted completion, and release the unused image through maintenance1.
Track whether acquire was already consumed; never wait on a binary semaphore twice.
For present errors, distinguish an enqueued request (including out-of-date/surface-lost)
from a failure to enqueue: only the former has a presentation completion producer.
If no present was enqueued, destroy/replace the unused signaled render-finished
semaphore after its GPU producer completes and release the image; do not signal it
again without a consuming wait, or wait on a nonexistent present fence.

References: [presentation semaphore lifetime](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html),
[presentation fences](https://docs.vulkan.org/refpages/latest/refpages/source/VkSwapchainPresentFenceInfoKHR.html),
[present error semantics](https://docs.vulkan.org/refpages/latest/refpages/source/vkQueuePresentKHR.html),
[unused-image release](https://docs.vulkan.org/refpages/latest/refpages/source/vkReleaseSwapchainImagesKHR.html),
[swapchain creation](https://docs.vulkan.org/refpages/latest/refpages/source/VkSwapchainCreateInfoKHR.html).

## Required evidence

- Delete an entity and release its last application resource handle while frames are in flight.
- Edit shared and cloned materials without overwriting a preceding frame's data.
- Repeatedly resize/minimize/restore and recreate GPU buffers, including imported CUDA buffers.
- Shut down with pending work, including a non-rendered compute stage.
- Exercise failed creation and failed replacement without leaking handles or publishing invalid state.
- Run GPU validation and inspect retirement/completion ordering; CPU tests alone cannot establish it.
- Check allocator split/coalesce/alignment/overflow and failed growth without Vulkan;
  verify actual memory types, atom boundaries, dedicated requirements and allocation
  counts on GPU. Record peak retained staging/descriptor capacity.
- Verify swapchain-maintenance availability on Windows and Linux; inject post-acquire
  abort and present/recreate failure paths, not only successful resize.

References: [Vulkan synchronization examples](https://docs.vulkan.org/guide/latest/synchronization_examples.html),
[buffer destruction requirements](https://docs.vulkan.org/refpages/latest/refpages/source/vkDestroyBuffer.html),
[Vulkan-Hpp](https://github.com/KhronosGroup/Vulkan-Hpp).
