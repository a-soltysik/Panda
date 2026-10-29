# Use cases and implementation flows

These documents connect application goals with Panda's implementation. Read the
positive path first, then follow the variants table to failures and cleanup.
Class and method names identify main participants; diagrams omit incidental
forwarding and helper calculations.

| Flow | Boundary |
| --- | --- |
| [UC Create a Context](create-context.md) | Vulkan instance, device and services; headless and windowed initialization, ownership and partial failure cleanup |
| [UC Select a compatible Vulkan device](select-vulkan-device.md) | Required capabilities, work and present queues, and window-surface support |
| [UC Create and release a native window](create-window.md) | Optional Panda::Tools/GLFW wrapper; applications may provide their own `WindowSurface` |
| [UC Run a window event loop](run-window-event-loop.md) | Process input and close requests; poll while active and wait while presentation is suspended |
| [UC Create a buffer](create-buffer.md) | Worked example: positive sequence, ownership and binding-failure cleanup for an ordinary buffer |
| [UC Create and import an external buffer](create-external-buffer.md) | Dedicated Vulkan allocation and optional CUDA import; ends before GPU work |
| [UC Alternate Vulkan and CUDA work on a shared buffer](alternate-shared-buffer-work.md) | Repeat Vulkan upload, CUDA work and Vulkan reacquisition on one buffer |
| [UC Allocate a compatible device-memory range](allocate-memory-range.md) | Compatible memory type, pooled or dedicated range, and bounded fallback |
| [UC Create a texture and its default view](create-texture.md) | Validate, allocate, bind and create the internal view before publishing a 2D image |
| [UC Create mesh storage with optional indices](create-mesh.md) | Derive sizes and publish vertex/optional index buffers only when all succeed |
| [UC Return a resource memory range](release-memory-range.md) | Final owner release, range coalescing and empty-block policy |
| [UC Upload bytes into a buffer](upload-buffer.md) | Snapshot, ordered buffer writes, asynchronous submission and retry behavior |
| [UC Snapshot caller bytes into batch-owned staging](snapshot-upload-staging.md) | Byte-staging path: aligned slices, noncoherent flush and caller-byte lifetime |
| [UC Upload texture contents or regions](upload-texture.md) | Full-image and pitched-region validation, one-batch reservation, image transitions and immutable submitted contents |
| [UC Upload a complete mesh payload into a batch](upload-mesh-payload.md) | Stage full vertex and optional index data before appending either write |
| [UC Observe or wait for submitted GPU work](observe-completion.md) | Context-scoped completion queries, bounded waits and upload retirement |
| [UC Reclaim completed upload packets](reclaim-completed-uploads.md) | Observe queue completion and release retained upload owners |
| [UC Render and present a diagnostic clear frame](render-clear-frame.md) | Acquire, record, submit and present; frame and image synchronization outcomes |
| [UC Collect paired CPU and GPU frame timings](collect-frame-timings.md) | Opt-in timestamp collection, CPU/GPU pairing and nonblocking sample retrieval |
| [UC Execute a controlled benchmark smoke run and save a report](benchmark-smoke-run.md) | Validate a stable clear-frame run, collect only measured samples and write JSON/log output |
| [UC Configure and receive application/backend diagnostics](configure-diagnostics.md) | Register synchronous log sinks, filter entries and receive Vulkan validation messages |
| [UC Suspend presentation and recreate the swapchain](recreate-swapchain.md) | Framebuffer changes, minimized windows and failed swapchain replacement |
| [UC Commit work to the Context submission timeline](commit-context-submission.md) | Queue acceptance and timeline value publication |
| [UC Retire an acquired image after frame work fails](retire-acquired-image.md) | Consume acquire semaphore and release an unused image |
| [UC Recover from a presentation failure after GPU submission](recover-presentation-failure.md) | Retire the GPU producer and replace unused present signaling |
| [UC Drain frame and presentation use](drain-presentation.md) | Wait for queue completion and presentation fences |
| [UC Shut down a Context safely](shutdown-context.md) | Release caller-owned handles, drain internal work and tear down Context services |
| [UC Query CUDA Runtime availability](query-cuda-availability.md) | Map Runtime device-count and initialization status to an availability result |

For authors, see the [conventions](../development/functional-documentation.md) and
[scenario template](template.md). The template is an authoring aid, not an
implemented feature. Shared guarantees remain in the
[resource contract](../design/contracts/resources.md) and other design contracts
listed in the [documentation index](../README.md).
