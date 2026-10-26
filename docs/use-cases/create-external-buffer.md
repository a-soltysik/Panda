# Create and import an external buffer

## Goal and boundary

An application asks Vulkan to create a shareable buffer and may import its memory
into `Panda::Cuda`. When supported, Core also exports a paired timeline-semaphore
handle. The application checks both capabilities before choosing a path during
startup. The flow ends with an `ExternalBuffer` or a `SharedCudaBuffer`.
It imports and maps shared memory and may import a semaphore handle, but does not
submit compute work or synchronize external GPU use. Follow the [shared-buffer task](../development/tasks/cuda-shared-buffers.md)
and [compute contract](../design/contracts/compute.md).

**The application** chooses the buffer's size and usage and keeps `Context` alive.
**Panda Core** creates a Vulkan buffer and exports an OS memory handle plus an
optional semaphore handle without depending on CUDA. **`Panda::Cuda`**, when used,
matches the Vulkan device and imports the allocation once. When the paired
semaphore handle is present, it imports that timeline semaphore too; neither import
operation synchronizes GPU use.

| Boundary | Meaning |
| --- | --- |
| Trigger and input | Application creates a Context with external-memory support and requests a `BufferDesc` |
| Successful end | An owning `ExternalBuffer` with an optional timeline-semaphore handle, optionally consumed into a `SharedCudaBuffer` |
| Failure end | A returned `Error`; unpublished Vulkan/CUDA state is released |
| Outside this flow | Compute submission and Vulkan/external-API synchronization |

## Main flow

1. **Check Vulkan capability.** The application creates `Context` with
   `ContextOptions::ExternalBufferSupport::Optional`. During device setup,
   `ExternalContext` enables the platform external-memory extension and enables
   external-semaphore export when the selected device supports it. The application
   immediately reads `Context::getDeviceInfo().externalBufferExportEnabled` and
   `externalSemaphoreExportEnabled`. Missing memory export selects its fallback;
   missing semaphore export still permits memory sharing but no safe cross-API use.
2. **Match the CUDA device.** If external export is enabled and the application
   chooses CUDA, it calls `CudaSession::create` before allocating a buffer. The
   add-on searches CUDA devices for the Context's UUID, selects the match and creates
   one non-blocking stream. No match selects the application's fallback path.
3. **Request a buffer.** The application calls `Context::createExternalBuffer`.
   `BufferResource::validateExportableDescription` checks size, usage and device-local
   memory intent before Vulkan is queried.
4. **Create and export.** `ExternalContext` checks export support for the requested
   usage. `BufferResource` creates a device-local Vulkan buffer and asks the
   `MemoryPool` for one dedicated allocation. Vulkan binds it at offset zero and
   exports the allocation handle. When semaphore export is supported,
   `ExternalContext` also creates an exportable timeline semaphore and exports its
   handle. Panda publishes an `ExternalBuffer` containing the Vulkan buffer, memory
   handle, optional semaphore handle, logical size, allocation size and device UUID.
5. **Import and retain.** The application moves the `ExternalBuffer` into
   `CudaSession::importBuffer`. The adapter imports the dedicated allocation and
   maps one view spanning the logical buffer size. When a paired semaphore handle
   is available, it imports and retains the timeline semaphore too. It does not yet
   queue semaphore waits or signals. `SharedCudaBuffer` retains the Vulkan resource,
   CUDA mapping, optional imported semaphore and session.

```mermaid
sequenceDiagram
    participant A as Application
    participant C as Context
    participant E as ExternalContext
    participant B as BufferResource
    participant M as MemoryPool
    participant V as Vulkan device
    participant S as CudaSession
    participant R as CUDA Runtime
    A->>C: Step 1 - create Context with optional external memory
    C->>V: Step 1 - query and enable external-memory and optional semaphore support
    C-->>A: Step 1 - deviceInfo memory and semaphore export flags
    A->>S: Step 2 - create(Context)
    S->>R: Step 2 - match device UUID and create stream
    R-->>S: Step 2 - matching CUDA session
    S-->>A: Step 2 - return CudaSession
    A->>C: Step 3 - createExternalBuffer(description)
    C->>E: Step 3 - validate exportable description
    E->>V: Step 4 - check export support for the requested usage
    E->>B: Step 4 - create exportable buffer
    B->>V: Step 4 - create Vulkan buffer with external-memory handle type
    B->>M: Step 4 - allocate dedicated device-local memory
    M->>V: Step 4 - allocate memory
    M-->>B: Step 4 - dedicated allocation at offset zero
    B->>V: Step 4 - bind buffer memory
    E->>V: Step 4 - export allocation handle
    opt Step 4 - semaphore export is supported
        E->>V: create exportable timeline semaphore
        E->>V: export semaphore handle
    end
    E-->>A: Step 4 - ExternalBuffer owns Buffer and memory handle, optional semaphore handle
    A->>S: Step 5 - importBuffer(move(ExternalBuffer))
    S->>R: Step 5 - import allocation and map logical buffer range
    R-->>S: Step 5 - CUDA mapping
    opt Paired timeline semaphore is available
        S->>R: Step 5 - import timeline semaphore
        R-->>S: Step 5 - imported semaphore
    end
    S-->>A: Step 5 - SharedCudaBuffer retains mapping and Vulkan resource
```

## Notes

**Ownership.** `Context` outlives the `ExternalBuffer` and every copied `Buffer` it
returns. A `SharedCudaBuffer` also retains its CUDA session; `CudaSession::close()`
returns `ResourcesInUse` while shared-buffer owners remain. Linux transfers the
allocation and semaphore FDs to CUDA on successful imports. Windows keeps both
exported HANDLEs under Panda's RAII wrappers until the `SharedCudaBuffer` releases
them. CUDA destroys its imported semaphore with the last `SharedCudaBuffer` owner.

**Memory is not ordering.** The OS handle identifies the same allocation to both
APIs. It does not signal completion, transfer Vulkan queue-family ownership or make
writes visible. Although `Panda::Cuda` imports a paired timeline semaphore when
present, it does not yet queue waits or signals on it. The current API does not expose
compute submission or a safe cross-API handoff, so import success is not permission
to use the mapping for GPU work. The [handoff task](../development/tasks/gpu-handoff.md)
owns that next boundary.

|  | Point and condition | Response | Resulting state and next action |
| --- | --- | --- | --- |
| **Variant&nbsp;1** | Step 1: optional external-memory support is unavailable | Context creation succeeds and device info reports `false` | Choose the application's fallback path before creating a shared buffer |
| **Variant&nbsp;2** | Step 1: memory export is available but semaphore export is not | Context reports `externalSemaphoreExportEnabled == false`; external buffers can still be created and imported | Use only the memory-sharing path; choose a fallback before any cross-API GPU work |
| **Error&nbsp;1** | Step 1: required platform/device external-memory support is unavailable | Return `Unsupported` during Context creation | No Context is published; use a compatible device or request optional support |
| **Error&nbsp;2** | Step 2: CUDA has no UUID matching the Vulkan device | Return `Unsupported` | No CUDA session is published; select the application's fallback before allocating |
| **Error&nbsp;3** | Step 3: size, usage or memory intent is invalid | Return the validation error before creating a Vulkan buffer | No native resource is published; correct the description |
| **Error&nbsp;4** | Step 4: the device cannot export the requested memory usage/handle | Return `Unsupported`; release unpublished state | No `ExternalBuffer` is published; choose a supported usage/device |
| **Error&nbsp;5** | Step 4: timeline semaphore creation or export fails after capability discovery | Return the Vulkan error and release unpublished semaphore, memory handle and allocation | No `ExternalBuffer` is published; investigate the driver error before retrying |
| **Error&nbsp;6** | Step 5: CUDA memory/semaphore import or mapping fails | Return the CUDA error and release any imported semaphore, allocation and mapping | No `SharedCudaBuffer` is published; retry only after correcting the reported cause |
| **Error&nbsp;7** | Closing the session while a shared buffer remains alive | Return `ResourcesInUse` | Session stays usable; release all `SharedCudaBuffer` copies before closing again |

**Success.** The allocation can be identified by another API and its ownership is
tracked. **Failure.** No partially initialized wrapper escapes; cleanup failures are
reported with the originating backend status where available.

## Related information

- [UC Create a buffer](create-buffer.md) explains the ordinary Vulkan-buffer path.
- [Resource ownership](../design/contracts/resources.md#ownership) explains Context
  and buffer lifetime requirements.
- [Compute and external-buffer contract](../design/contracts/compute.md) separates
  memory import from the later synchronization and submission APIs.
- [CUDA external resources](https://docs.nvidia.com/cuda/cuda-runtime-api/group__CUDART__EXTRES__INTEROP.html)
  explains CUDA handle-import ownership and mapping requirements.
- [CUDA connection example](../../examples/cuda_connection/README.md) demonstrates
  optional import and the current boundary before cross-API GPU use.
