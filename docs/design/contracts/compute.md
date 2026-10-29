# Compute and external buffer contract

This contract keeps Panda focused on Vulkan graphics while allowing applications
to add GPU computation. The [plan](../../development/PLAN.md) tracks implementation
and proof; the [handoff task](../../development/tasks/gpu-handoff.md) owns the
single-buffer submission path, and the later compute-integration task owns its use
in a multi-step application workload.

## Responsibility boundary

The application owns its algorithms, simulation state, timestep, number of
simulation steps, and the point in its update loop when computation and rendering
are requested. Panda does not decide whether a simulation runs once per frame,
multiple times, or only when input changes.

Panda Core owns Vulkan resources and exposes generic external-buffer memory handles
and GPU ownership handoffs through `ExternalBufferManager`. It does not name CUDA
types or require CUDA. `Panda::Cuda` is an optional add-on that matches a CUDA device
to the selected Vulkan device and imports a compatible buffer.
Other adapters may use the same external-buffer contract when their import API
accepts the platform's supported opaque FD or Win32 handle type.

Do not add a persistent callback registry, per-frame compute scheduler, entity-based
compute system, or general task graph. The application explicitly submits one
bounded batch for a buffer handoff, then decides when to render or submit another
batch.

## Current external-buffer behavior

`ContextOptions::ExternalBufferSupport` is `Disabled` by default. `Optional` enables
the platform external-memory extension when available; `Required` returns
`Unsupported` when the selected Vulkan device cannot enable it. This option has no
CUDA dependency. With `Optional`, the application reads
`Context::getDeviceInfo().externalBufferExportEnabled` immediately after creation:
`true` means the external-memory extension was enabled for that Context; `false`
means to choose another path before allocating a shared buffer. `Required` fails
Context creation instead of returning `false`.

The flag describes Context-level Vulkan export setup only. Support for an individual
buffer usage is queried when that buffer is requested. CUDA applications can also
create `CudaSession` before allocating buffers; failure to match the selected Vulkan
device is reported then, so they can select an application-owned fallback early.
`ContextDeviceInfo::externalSemaphoreExportEnabled` separately reports whether the
selected device can export the opaque timeline semaphore handle used by the handoff.

`ExternalBufferManager::createBuffer(BufferDesc)` validates the request, checks
export support for the requested Vulkan usage, creates a device-local buffer backed
by one dedicated allocation, binds it at offset zero and exports the platform
allocation handle. The resulting `ExternalBuffer` owns both the Vulkan `Buffer` and one
move-only `ExternalMemoryHandle`, plus the logical size, allocation size and Vulkan
device UUID. When `externalSemaphoreExportEnabled` is true, it also owns a paired
timeline semaphore with initial value 0 and its move-only `ExternalSemaphoreHandle`.
The semaphore handle can be absent on devices without export support. Ordinary
`Context::createBuffer` resources remain non-exportable.

`Panda::Cuda::CudaSession::create(Context&)` selects a CUDA device by UUID and
creates one non-blocking stream. `CudaSession::importBuffer(ExternalBuffer)` imports
and maps the dedicated allocation once, and imports the paired timeline semaphore
when one is present. `SharedCudaBuffer` retains the imported allocation and
semaphore, its Vulkan buffer and the CUDA session; `close()` reports
`ResourcesInUse` while those owners remain alive. On Linux, successful FD import
transfers handle ownership to CUDA; on Windows, the exported HANDLE remains owned by
its RAII wrapper until the `SharedCudaBuffer` releases it. CUDA destroys the imported
semaphore when the last shared-buffer owner is released.

`ExternalBufferManager::releaseToExternal(Buffer)` records a Vulkan queue-family
release and signals the paired timeline semaphore. `acquireFromExternal(Buffer,
value)` queues a wait for the next timeline value and records the matching acquire;
neither operation waits on the CPU. The Buffer view can come from either
`ExternalBuffer` or `SharedCudaBuffer`, so ownership can be reacquired after import.
The public handoff methods use `ExternalTimelineValue` instead of an untyped integer.
`SharedCudaBuffer::submitWork(releaseValue, callback)` queues a CUDA wait for the
Vulkan release value, invokes the callback with the matched session's `cudaStream_t`,
a borrowed `std::byte*` device address and the logical buffer size, then queues a
signal at `releaseValue + 1`. Keep buffer access on the supplied stream so the two
semaphore operations order the application's work. The mapping and stream are
borrowed only for the callback; the shared buffer retains their owners while alive.
The method reports enqueue completion, not GPU completion, and performs no CPU or
device-wide wait. Its result carries both the signal value and callback enqueue
result. A callback error does not cancel the signal: the application must still
queue `acquireFromExternal` with that value before acting on the callback error.
Submissions for a shared buffer must be serialized and use the next release value.
Wait or signal API failures are returned as operation errors; a failed signal faults
that imported handoff so it cannot be resubmitted unsafely. GPU-side data ordering
still requires hardware proof in the [handoff task](../../development/tasks/gpu-handoff.md).

## Required synchronization contract

Memory sharing and lifetime are not synchronization. Keeping a buffer alive does
not order its producers and consumers, and CPU call order does not order work on
different GPU queues or APIs.

Before any external backend writes a buffer, all earlier Vulkan readers and writers
must complete and the required memory visibility and queue-family ownership must be
established. Before Vulkan reads it again, the external write must complete and its
results must be visible to Vulkan. The reverse direction is required when Vulkan
writes data later consumed by the external backend. Rendering includes every pass
that reads the resource, including shadows.

The [external handoff task](../../development/tasks/gpu-handoff.md) proves the
smallest supported GPU-side handoff for the optional CUDA add-on. It preserves
application-owned scheduling, avoids CPU copies and routine global waits, and defines
failure and resource-retirement behavior. The later
[compute integration task](../../development/tasks/compute-api.md) demonstrates
multi-step application work using this handoff.

## Build and capability boundary

- With CUDA disabled, CMake does not discover the CUDA toolkit or add CUDA headers
  and libraries to Panda Core.
- With CUDA enabled, runtime/device absence is reported by the optional add-on and
  does not make ordinary Vulkan rendering unavailable.
- Unsupported external handle or usage combinations return `Unsupported`; there is
  no silent host-copy fallback or runtime device migration.
- Sharing buffers is the initial scope. External images, device groups and a
  generic render hardware interface are out of scope.

## References

- [Resource ownership](resources.md#ownership) explains Context and resource
  lifetimes.
- [Resource allocation](resources.md#allocation-uploads-and-descriptors) explains
  why exportable allocations are dedicated and not suballocated from shared blocks.
- [CUDA external resources](https://docs.nvidia.com/cuda/cuda-runtime-api/group__CUDART__EXTRES__INTEROP.html)
  describes CUDA's platform import and destruction requirements.
