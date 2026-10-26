# Create a Context

## Goal and boundary

An application creates a Vulkan `Context` by calling `Context::create()` for a
headless context or `Context::createWithSurface()` for a windowed context. This
implemented flow runs synchronously, may allocate or block, and ends when Panda
publishes a fully initialized, move-only owner or returns an error. The windowed
path also initializes presentation resources and an initial swapchain generation
when the framebuffer has a nonzero extent; a zero extent leaves presentation
pending while the Context itself can still succeed.

**Panda** owns the Vulkan instance, device, native surface and Context services.
**The application** owns the `WindowSurface` and native window and keeps both alive
for the full lifetime of a Context created from them. The flow submits no frame or
upload work.

## Preconditions

1. **Windowed path.** When using `Context::createWithSurface()`, provide a live
   `WindowSurface`; calls to its methods follow its thread requirements and happen
   outside its callbacks.

## Main flow

1. **Choose the Context path.** The application supplies options and, for the
   windowed path, a live `WindowSurface` whose required instance extensions include
   `VK_KHR_surface` and the platform surface extension.
2. **Build instance services.** Panda verifies Vulkan 1.3 support from the
   loader, checks the validation layer when requested, resolves the required
   instance extensions, and creates the Vulkan instance. It then creates the
   optional debug messenger and, for a windowed Context, creates and owns the
   native `VkSurfaceKHR` returned by `WindowSurface`.
3. **Select a compatible device.** Panda calls the
   [UC Select a compatible Vulkan device](select-vulkan-device.md), which returns a physical
   device, queue families and the capabilities needed by this Context.
4. **Check external-buffer support.** When
   `ContextOptions::externalBufferSupport` is not `Disabled`, Panda checks whether
   the selected device exposes the platform external-memory extension. `Required`
   support fails if it is unavailable; `Optional` support continues without
   external-buffer services. If the memory extension is available, Panda also
   checks whether the device can export a timeline-semaphore handle. Missing
   semaphore support still permits external-memory buffers, but not a safe
   cross-API GPU handoff.
5. **Create the logical device.** Panda enables the queried Vulkan 1.3
   features and creates the selected graphics/compute queue. A windowed Context
   also enables the selected present queue and swapchain-maintenance extension.
   When the platform external-memory extension is available, it enables that
   extension and enables the external-semaphore extension only if timeline-semaphore
   export is supported.
6. **Initialize Context services.** Panda creates the memory pool and
   performance manager, and creates `ExternalContext` when external-memory export
   is available. For a windowed Context it also initializes presentation frame
   resources and reads the framebuffer extent. A nonzero extent creates the
   initial swapchain generation; a zero extent records that presentation must be
   prepared later.
7. **Publish the owner.** Only after initialization succeeds does
   `createInternal()` construct and return the public `Context`.

The paths share the same creation steps but differ in instance and presentation
setup, so each entry point has its own sequence.

### Headless path

```mermaid
sequenceDiagram
    participant A as Application
    participant C as Context
    participant I as Context::Impl
    participant V as Vulkan instance and device
    participant D as Device selection
    A->>C: Step 1 - create(options)
    C->>I: Initialize without a surface
    I->>V: Step 2 - Verify loader and optional validation layer
    I->>V: Create instance and optional debug messenger
    I->>D: Step 3 - Select headless-compatible physical device and queue
    Note over I,D: Step 3 - UC Select a compatible Vulkan device
    D-->>I: Selected device, capabilities and queue families
    opt External-buffer support requested
        I->>V: Step 4 - Check platform external-memory extension
        V-->>I: External-memory extension availability
        opt External-memory extension available
            I->>V: Query optional timeline-semaphore export
            V-->>I: Timeline-semaphore export availability
        end
    end
    I->>V: Step 5 - Create logical device with selected features, queues and supported extensions
    I->>I: Step 6 - Create memory pool, optional ExternalContext and performance manager
    I->>C: Step 7 - Publish fully initialized Context
    C-->>A: Result containing Context
```

### Windowed path

```mermaid
sequenceDiagram
    participant A as Application
    participant C as Context
    participant I as Context::Impl
    participant W as WindowSurface
    participant V as Vulkan instance and device
    participant D as Device selection
    participant P as Presentation
    A->>C: Step 1 - createWithSurface(window, options)
    C->>I: Initialize with borrowed WindowSurface
    I->>V: Step 2 - Verify loader and optional validation layer
    I->>W: Get required instance extensions
    W-->>I: Platform and surface extension names
    I->>V: Create instance and optional debug messenger
    I->>W: Create native Vulkan surface
    W-->>I: Transfer native surface ownership to Context
    I->>D: Step 3 - Select surface-compatible physical device and queues
    Note over I,D: Step 3 - UC Select a compatible Vulkan device
    D-->>I: Selected device, capabilities and queue families
    opt External-buffer support requested
        I->>V: Step 4 - Check platform external-memory extension
        V-->>I: External-memory extension availability
        opt External-memory extension available
            I->>V: Query optional timeline-semaphore export
            V-->>I: Timeline-semaphore export availability
        end
    end
    I->>V: Step 5 - Create logical device with selected features, queues and supported extensions
    I->>I: Step 6 - Create memory pool, optional ExternalContext and performance manager
    I->>P: Initialize frame resources and initial presentation
    P->>W: Query framebuffer extent
    W-->>P: Pixel extent
    alt Nonzero framebuffer extent
        P->>P: Create initial swapchain generation
    else Zero framebuffer extent
        P->>P: Leave generation pending
    end
    P-->>I: Initial generation or zero-extent pending state
    I->>C: Step 7 - Publish fully initialized Context
    C-->>A: Result containing Context
```

## Notes

**Capability checks.** Requesting Vulkan 1.3 at instance creation does not prove
that a physical device exposes every feature Panda needs. Device selection queries
and enables dynamic rendering, synchronization2 and timeline semaphores explicitly.
Windowed contexts additionally require compatible surface support and swapchain
maintenance1. External-buffer support is resolved after core device selection and
before logical-device creation. Context setup checks the platform memory extension,
then checks optional timeline-semaphore export when memory export is available;
the requested buffer usage is checked later when the buffer is created. The full
core-device rules live in [UC Select a compatible Vulkan device](select-vulkan-device.md).

**Ownership.** `WindowSurface` is borrowed, while the native surface it creates is
owned by the Context. Partial initialization never publishes a facade: if a later
step fails, the local implementation and its already-created RAII owners unwind,
releasing device-side objects before the surface and instance. The caller still
owns the window and must keep it alive until Context teardown.

**Windowed readiness.** A successful windowed Context has presentation frame
resources. If the framebuffer is minimized and reports zero pixels, creation
succeeds with presentation pending and no zero-sized swapchain generation. Later
window-frame preparation handles that state; see the
[window lifecycle contract](../design/contracts/resources.md#frame-and-window-state).

**Headless creation.** `Context::create()` follows the same loader, device and
service setup without querying window extensions, creating a surface or initializing
presentation. Upload support is initialized when an upload batch is first requested.

## Variants and errors

|  | Point and condition | Response | Resulting state and next action |
| --- | --- | --- | --- |
| **Error&nbsp;1** | Step 2: loader is below Vulkan 1.3, or a requested validation layer/extension is unavailable | Return `Unsupported`; extension enumeration and provider errors keep their reported error | No Context is published; install or select a compatible Vulkan runtime, or disable optional validation |
| **Error&nbsp;2** | Step 2: required window extensions are malformed or unavailable, or instance, messenger or native-surface creation fails | Return `InvalidArgument`, `Unsupported` or the originating native/provider error; release any instance services already created | No Context is published; correct the provider/configuration or resolve the reported failure |
| **Error&nbsp;3** | Step 3: no compatible physical device is found, or device inspection cannot complete | Propagate the selection result; see [UC Select a compatible Vulkan device](select-vulkan-device.md#variants-and-errors) | No logical device or Context is published; respond to the reported capability or backend failure |
| **Variant&nbsp;1** | Step 4: optional external-memory support is unavailable | Continue Context creation without `ExternalContext`; device info reports external-buffer export as disabled | Use the application's fallback path; see [UC Create and import an external buffer](create-external-buffer.md) |
| **Variant&nbsp;2** | Step 4: external-memory export is available but timeline-semaphore export is not | Continue Context creation and enable external-memory export only | Memory sharing remains available; cross-API GPU handoff is unsupported |
| **Error&nbsp;4** | Step 4: required external-memory support is unavailable, or device-extension inspection fails | Return `Unsupported` for unavailable required support or propagate the inspection error | No logical device or Context is published; choose a compatible device or resolve the reported backend failure |
| **Error&nbsp;5** | Step 5: logical-device creation fails | Return the mapped Vulkan error without adopting a failed native output | No Context is published; retry only after resolving the reported cause |
| **Error&nbsp;6** | Step 6: performance-manager initialization fails | Return its error and release the local manager and other Context owners already created | No Context is published; investigate the reported device/backend failure before retry |
| **Error&nbsp;7** | Step 6: windowed presentation initialization fails | Return the originating error and release partial presentation state and Vulkan owners; see [Error 7 cleanup](#error-7---release-partial-presentation-state) | No Context is published; no application Context is available |

### Error 7 - Release partial presentation state

**Entry state.** Steps 1–5 completed. The private implementation owns instance services,
the selected physical device wrapper, the logical device, memory pool and initialized
performance manager. Presentation setup may have created frame resources or attempted
the initial swapchain generation. No public Context has been published and this
flow has submitted no GPU work.

```mermaid
sequenceDiagram
    participant A as Application
    participant C as Context::createInternal
    participant I as Context::Impl
    participant P as Presentation::create
    participant D as Logical device and Context services
    participant S as Owned surface and instance services
    C->>I: Step 6 - Initialize Context services
    I->>P: Create frame resources and initial generation
    P-->>I: Error 7 - Initialization error
    Note over P: The unpublished Presentation owner releases its partial state
    I-->>C: Propagate Error, no Context facade exists
    Note over C,I: The unpublished Impl releases its remaining RAII owners
    I->>D: Release services and logical device
    I->>S: Release owned surface, messenger and instance
    C-->>A: Result containing Error
```

The error does not promise byte-for-byte rollback of driver or window-system
state. It guarantees that Panda returns no partially usable `Context` and releases
the Vulkan owners created by this attempt.

## Outcomes

On success, the application owns one valid `Context` and may move it while no
operation is in progress. A windowed Context also borrows its `WindowSurface` and
native window for the entire Context lifetime. Context destruction drains its
owned work and requires the application to release resource handles and unsubmitted
upload batches first; see the [ownership contract](../design/contracts/resources.md#ownership).

On failure, the caller receives an `Error` and no Context owner. Error categories
describe the failure and recovery direction; they do not guarantee that retrying
against the same runtime or surface will succeed.

## Related information

- [UC Create and release a native window](create-window.md) explains the GLFW
  lifetime and the `WindowSurface` owner borrowed by this flow.
- [UC Select a compatible Vulkan device](select-vulkan-device.md) explains the
  capability and queue-family decision used at Step 3.
- [UC Create and import an external buffer](create-external-buffer.md) explains
  how the optional export capabilities are used after Context creation.
- [Resource ownership](../design/contracts/resources.md#ownership) defines the
  required Context, resource and window lifetimes.
- [Frame and window state](../design/contracts/resources.md#frame-and-window-state)
  explains windowed readiness and the later presentation lifecycle.
- [Vulkan context and window lifecycle](../development/tasks/vulkan-context.md)
  records the accepted Context scope and its implementation evidence.
- The Vulkan Guide's [version and feature rules](https://docs.vulkan.org/guide/latest/versions.html)
  explain why version requests and queried feature bits are separate checks; its
  [WSI guide](https://docs.vulkan.org/guide/latest/wsi.html) describes the native
  surface boundary.
