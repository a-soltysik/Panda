# Create and import an external buffer

## Goal and boundary

An application asks Vulkan to create a shareable buffer and may import its memory
into `Panda::Cuda`. When supported, Core also exports a paired timeline-semaphore
handle and can queue Vulkan ownership releases and acquires through
`ExternalBufferManager`. The CUDA add-on imports and retains the allocation and
semaphore and has private stream wait/signal operations. The application-facing
compute submission path is still pending.
Follow the [shared-buffer task](../development/tasks/cuda-shared-buffers.md) and
[compute contract](../design/contracts/compute.md).

**The application** chooses the buffer's size and usage and keeps `Context` alive.
**Panda Core** creates a Vulkan buffer and exports an OS memory handle plus an
optional semaphore handle without depending on CUDA. **`Panda::Cuda`**, when used,
matches the Vulkan device and imports the allocation once. When the paired
semaphore handle is present, it imports that timeline semaphore too. Core handoff
calls queue Vulkan barriers and semaphore operations; import alone does not enqueue
CUDA work or synchronize GPU use.

| Boundary | Meaning |
| --- | --- |
| Trigger and input | Application creates a Context with external-memory support and requests a `BufferDesc` |
| Successful end | An `ExternalBuffer` or `SharedCudaBuffer`; Core can queue release and acquire for its Vulkan buffer view |
| Failure end | A returned `Error`; unpublished Vulkan/CUDA state is released |
| Outside this flow | Application CUDA work submission |

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
3. **Request a buffer.** The application calls
   `Context::getExternalBufferManager().createBuffer`.
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
   is available, it imports and retains the timeline semaphore too. Private stream
   wait/signal operations exist, but this public flow cannot yet invoke them around
   application compute work. `SharedCudaBuffer` retains the Vulkan resource,
   CUDA mapping, optional imported semaphore and session.
6. **Release Vulkan ownership.** The application passes
   `SharedCudaBuffer::getBuffer()` to `ExternalBufferManager::releaseToExternal`. Core records a
   queue-family release, signals the paired semaphore and returns its timeline
   value. Panda upload calls are rejected while another API owns the buffer.
7. **Reacquire Vulkan ownership.** After the external API has waited for the
   release value, completed its work and signaled the next value, the application
   passes the same `Buffer` view to `acquireFromExternal`. Core queues the wait and
   matching acquire barrier; later Vulkan queue work follows it without a CPU wait.
   This public flow cannot yet enqueue the CUDA wait or signal around application
   work, so its imported mapping is not ready for GPU work.

```mermaid
sequenceDiagram
    participant A as Application
    participant C as Context
    participant X as ExternalBufferManager
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
    A->>X: Step 3 - createBuffer(description)
    X->>E: Step 3 - validate exportable description
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
    A->>X: Step 6 - releaseToExternal(SharedCudaBuffer.getBuffer())
    X->>V: Step 6 - release barrier and signal timeline value N
    V-->>A: Step 6 - return N for external wait
    Note over A,R: Application CUDA work path remains pending
    A->>X: Step 7 - acquireFromExternal(Buffer view, N+1)
    X->>V: Step 7 - wait timeline N+1 and acquire barrier
```

## Notes

**Ownership.** `Context` outlives the `ExternalBuffer` and every copied `Buffer` it
returns. A `SharedCudaBuffer` also retains its CUDA session; `CudaSession::close()`
returns `ResourcesInUse` while shared-buffer owners remain. Linux transfers the
allocation and semaphore FDs to CUDA on successful imports. Windows keeps both
exported HANDLEs under Panda's RAII wrappers until the `SharedCudaBuffer` releases
them. CUDA destroys its imported semaphore with the last `SharedCudaBuffer` owner.

**Memory is not ordering.** The OS handle identifies the same allocation to both
APIs. Core's release/acquire calls submit the Vulkan queue-family barriers and
timeline semaphore operations. `Panda::Cuda` has private stream wait/signal helpers,
but this public flow cannot yet use them around application work. Import success is
not permission to use the mapping for GPU work. The
[handoff task](../development/tasks/gpu-handoff.md) owns the remaining cross-API
work path and proof.

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
| **Error&nbsp;8** | Step 6: another Context owns the buffer, the semaphore is unavailable, or an upload batch is still open | Return `InvalidArgument`, `Unsupported` or `ResourcesInUse` before submitting | Keep Vulkan ownership and resolve the reported condition before retrying |
| **Error&nbsp;9** | Step 7: the buffer was not released or the external timeline value is not the next value | Return `InvalidArgument` before submitting | Keep external ownership; provide the value signaled by the external API |

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
