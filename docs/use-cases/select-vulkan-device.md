# Select a compatible Vulkan device

## Goal and boundary

During Context creation, Panda must choose a physical device that can run the
Context's ordered graphics/compute work and, when a surface is present, present to
that surface. The `selectDevice()` subflow ends with one compatible physical device,
its queue-family indices, selected swapchain-maintenance variant and diagnostic
properties, or an error. Creating the logical device is the next step in
[Context creation](create-context.md).

This is implemented behavior for both headless and windowed Contexts. The selection
uses capabilities queried from each physical device; it does not promise the
highest-performing adapter. It prefers the first compatible discrete GPU and then
falls back to the first compatible device in Vulkan enumeration order.

## Preconditions

The Vulkan instance is live. For a windowed Context, its owned `VkSurfaceKHR` and
the resolved instance extensions are available during selection, including
`VK_KHR_get_surface_capabilities2` and at least one KHR or EXT surface-maintenance
variant. Selection is synchronous as part of `Context::create()` or
`Context::createWithSurface()`. S2 and S3 filter physical devices against the
device-side feature, extension, queue and surface requirements.

## Main flow

1. **S1 - Enumerate physical devices.** `selectDevice()` asks the Vulkan instance
   for the physical devices exposed by the implementation.
2. **S2 - Filter device capabilities.** Panda requires a device API version of at
   least 1.3 and queries support for timeline semaphores, dynamic rendering and
   synchronization2. A windowed candidate must also support
   `VK_KHR_swapchain`, a compatible KHR or EXT `swapchain_maintenance1` variant,
   and its maintenance feature bit.
3. **S3 - Check queues and surface support.** Every selected work queue family must
   support both graphics and compute. Headless selection takes the first such
   family. Windowed selection prefers one family that also supports presentation;
   if none does, it uses the first graphics/compute family and first present family
   as a pair. The windowed candidate must also expose at least one surface format
   and present mode.
4. **S4 - Choose and report one candidate.** Among compatible candidates, Panda
   chooses the first discrete GPU if present, otherwise the first candidate. It
   returns driver/device details, queue families, timestamp properties and the
   selected maintenance variant to `Context::Impl`.

```mermaid
sequenceDiagram
    participant C as Context::Impl
    participant S as Device selection
    participant I as Vulkan instance
    participant D as Physical device
    C->>S: S1 - selectDevice(instance, optional surface, extensions)
    S->>I: Enumerate physical devices
    I-->>S: Candidate devices
    loop Each physical device
        S->>D: S2 - Query API version, extensions, features and properties
        D-->>S: Capability and diagnostic properties
        alt Required capabilities are supported
            S->>D: S3 - Find graphics/compute queue family
            opt Windowed Context
                S->>D: Check presentation support for queue families
                S->>D: Query surface formats and present modes
            end
            D-->>S: Queue and surface support
            alt Candidate can run this Context
                S->>S: Keep compatible candidate
            else Candidate lacks required queue or surface support
                S->>S: Skip candidate
            end
        else Candidate lacks required API, extension or feature
            S->>S: Skip candidate
        end
    end
    S->>S: S4 - Prefer first discrete candidate, otherwise first candidate
    S-->>C: Selected physical device, queues and properties
```

## Notes

**Window and headless requirements differ.** Both paths require a Vulkan 1.3
physical device, the three core feature bits, and a graphics/compute queue family.
Only the windowed path checks surface presentation, formats, present modes,
`VK_KHR_swapchain` and swapchain maintenance1. The KHR maintenance variant is
preferred when both KHR and EXT variants are available.

**Queue choice.** Panda prefers a single family for graphics, compute and
presentation. If the surface requires a separate present family, both indices are
returned and the logical device requests both queues. This makes surface
compatibility part of device selection rather than a later frame-time surprise.

**Adapter inspection errors.** A device that simply lacks a capability is skipped.
An inspection error is logged and that adapter is skipped for most error
categories, allowing another candidate to qualify. `SurfaceLost` and
`InvalidArgument` are fatal to the whole selection because they describe the
window/surface request rather than one adapter.

## Variants and failures

| ID | Point and condition | Response | Resulting state and next action |
| --- | --- | --- | --- |
| **E1** | S1: physical-device enumeration fails | Propagate the instance-level Vulkan error | No candidate is inspected; Context creation fails with no logical device |
| **E2** | S2-S3: device lacks a required capability, queue, extension or usable surface format/present mode | Skip that device without treating the capability mismatch as an API error | Other devices remain eligible; if none qualify, continue to E5 or E6 |
| **E3** | S2-S3: device inspection returns `SurfaceLost` or `InvalidArgument` | Stop selection and propagate the error | Context creation fails; surface repair or correcting the request is required before retry |
| **E4** | S2-S3: another adapter inspection error occurs, and a later device is usable | Log and skip the failing adapter; return the compatible device | Context creation may proceed with the later device |
| **E5** | S2-S3: no candidate qualifies, and at least one adapter inspection failed | Return the first recorded inspection error after all candidates are checked | Context creation fails with that error; resolve the backend/device issue before retry |
| **E6** | S2-S3: no candidate qualifies and no inspection error occurred | Return `Unsupported` with a headless- or windowed-capability message | No logical device is created; use a compatible device/runtime or headless path |

`DeviceLost`, `Timeout`, `ResourceExhausted`, `BackendFailure` and other
nonfatal-to-selection inspection errors follow the skip-and-remember path. If a
later adapter qualifies, the first remembered error does not prevent its use.

## Outcomes

On success, `Context::Impl` receives one candidate whose required features were
queried and whose work/presentation queues meet the selected path. The following
logical-device creation step enables the queried features and creates the returned
queues.

On failure, no physical-device candidate is published to the public API and the
Context creation flow returns an error. Selection does not silently fall back to an
older presentation protocol when a windowed device lacks maintenance1 support.

## Related information

- [Create a Context](create-context.md) uses this result to create the logical
  device and Context services.
- The Vulkan specification's [device and queue chapter](https://docs.vulkan.org/spec/latest/chapters/devsandqueues.html)
  explains physical-device queries, queue families and logical-device creation.
- The Vulkan specification's [feature chapter](https://docs.vulkan.org/spec/latest/chapters/features.html)
  explains querying and enabling feature bits.
- The Vulkan specification's [window-system integration chapter](https://docs.vulkan.org/spec/latest/chapters/VK_KHR_surface/wsi.html)
  defines surface presentation support and capabilities.
