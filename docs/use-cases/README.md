# Use cases and implementation flows

These documents connect application goals with Panda's implementation. Read the
positive path first, then follow the variants table to failures and cleanup.
Class and method names identify main participants; diagrams omit incidental
forwarding and helper calculations.

| Flow | Boundary |
| --- | --- |
| [Create a Context](create-context.md) | Vulkan instance, device and services; headless and windowed initialization, ownership and partial failure cleanup |
| [Select a compatible Vulkan device](select-vulkan-device.md) | Shared Context-creation subflow: required capabilities, work/present queues and window support |
| [Create and release a native window](create-window.md) | Optional Panda::Tools/GLFW wrapper; applications may provide their own `WindowSurface` |
| [Create a buffer](create-buffer.md) | Worked example: positive sequence, ownership and binding-failure cleanup for an ordinary buffer |
| [Upload bytes into a buffer](upload-buffer.md) | Snapshot, ordered buffer writes, asynchronous submission and retry behavior |
| [Observe or wait for submitted GPU work](observe-completion.md) | Context-scoped completion queries, bounded waits and upload retirement |
| [Reclaim completed upload packets](reclaim-completed-uploads.md) | Shared subflow: observe queue completion and release retained upload owners |
| [Render and present a diagnostic clear frame](render-clear-frame.md) | Acquire, record, submit and present; frame and image synchronization outcomes |
| [Commit work to the Context submission timeline](commit-context-submission.md) | Shared subflow: queue acceptance and timeline value publication |
| [Retire an acquired image after frame work fails](retire-acquired-image.md) | Shared subflow: consume acquire semaphore and release an unused image |
| [Recover from a presentation failure](recover-presentation-failure.md) | Shared subflow: retire the GPU producer and replace unused present signaling |
| [Drain frame and presentation use](drain-presentation.md) | Shared subflow: wait for queue completion and presentation fences |

For authors, see the [conventions](../development/functional-documentation.md) and
[scenario template](template.md). The template is an authoring aid, not an
implemented feature. Shared guarantees remain in the
[resource contract](../design/contracts/resources.md) and other design contracts
listed in the [documentation index](../README.md).
