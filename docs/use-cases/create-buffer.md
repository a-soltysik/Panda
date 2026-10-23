# Create a buffer

## Goal and boundary

An application requests fixed-size, ordinary GPU buffer storage by calling
`Context::createBuffer(BufferDesc)`. This implemented flow ends with a published
`Buffer` or an error. It may allocate host/device memory, but submits no GPU work
and does not wait for GPU completion. Initial bytes are **undefined**; uploading and
synchronizing future uses are separate operations.

**Panda** owns native creation, memory selection, binding and cleanup. **The application**
chooses size, usage and memory intent, and keeps `Context` alive for every buffer owner.
The implemented scope follows [Allocation, uploads and resource lifetimes](../development/tasks/resource-lifetimes.md).

| Boundary          | Meaning                                                                              |
|-------------------|--------------------------------------------------------------------------------------|
| Trigger and input | Application calls `Context::createBuffer` with a `BufferDesc`                        |
| Successful end    | A `Result<Buffer>` containing an owning, memory-bound `Buffer`                       |
| Failure end       | A returned `Error` with no new `Buffer`, or a fatal caller-contract violation        |
| Outside this flow | Uploading bytes, submitting GPU work, synchronization of later uses and CUDA sharing |

## Preconditions

1. **Context.** Use a live `Context` and serialize creation with its other
   operations on the context execution thread.

## Main participants

| Participant                   | Responsibility in this flow                                                                |
|-------------------------------|--------------------------------------------------------------------------------------------|
| Application                   | Supplies the description and owns the returned `Buffer`                                    |
| `Context` / `Context::Impl`   | Supplies stable device/pool services, collects uploads and publishes the public handle     |
| `UploadManager`, when present | Checks submitted uploads and releases confirmed completed packets                          |
| `BufferResource`              | Validates the description and owns unpublished native state through allocation and binding |
| `MemoryPool`                  | Chooses memory and returns an owning allocation for a compatible range                     |
| Vulkan device                 | Creates the native buffer, reports its requirements and binds memory                       |

## Main flow

1. **Request.** The application calls `Context::createBuffer(description)`.
   `Context` requires live internal state.
2. **Release completed upload resources.** `Context::Impl::createResource`
   first calls `collectUploads()`. When an `UploadManager` exists, it uses the
   [completed-upload subflow](reclaim-completed-uploads.md) to reclaim packets
   whose completion values are no greater than the queue's observed completed
   value. It does not wait for packets with greater values; without a manager,
   this step is a no-op.
3. **Validate.** `BufferResource::create` checks that size is nonzero, usage is
   nonempty and supported, and memory intent is `Device`, `Upload` or `Readback`
   before native buffer creation. Supported usages are the ordinary transfer,
   vertex, index, indirect, uniform/storage and texel-buffer flags.
4. **Create native storage object.** A native Vulkan buffer is created and
   adopted into unpublished `BufferResource` state. It is not yet memory-bound.
5. **Allocate.** `BufferResource::bind` queries ordinary and dedicated memory
   requirements, translates memory intent to required/preferred properties and
   calls `MemoryPool::allocate`. The returned `MemoryAllocation` is moved into
   the resource. Pool selection, range placement and growth are summarized here;
   their policy is in the [allocation contract](../design/contracts/resources.md#allocation-uploads-and-descriptors).
6. **Bind.** `bindBufferMemory2` binds the buffer at the allocation's offset.
7. **Publish.** Only after successful binding does `Context` wrap the shared
   resource state in a public `Buffer` and return it.

```mermaid
sequenceDiagram
    participant A as Application
    participant C as Context / Impl
    participant U as UploadManager
    participant R as BufferResource
    participant P as MemoryPool
    participant V as Vulkan device
    A ->> C: Step 1 - createBuffer(description)
    C ->> C: Step 2 - collectUploads()
    opt UploadManager exists
        C ->> U: Step 2 - collect()
        Note over U: Checks GPU completion without waiting
        Note over U: Releases packets for completed transfers
        U -->> C: Success
    end
    C ->> R: Step 3 - BufferResource::create(...)
    Note over R: Validate description before creating a native buffer
    R ->> V: Step 4 - createBuffer(...)
    V -->> R: Native buffer
    Note over R: Unpublished state owns the native buffer
    R ->> V: Step 5 - getBufferMemoryRequirements2(...)
    V -->> R: Memory and dedicated requirements
    R ->> P: Step 5 - allocate(request)
    P -->> R: MemoryAllocation ownership
    R ->> V: Step 6 - bindBufferMemory2(...)
    V -->> R: Success
    R -->> C: Shared resource state
    C -->> A: Step 7 - Result containing Buffer
```

## Notes

**Publication guarantee.** The *publication point* is Step 7: no application handle escapes
before allocation and binding succeed. Earlier native success does not imply operation success.

**Lazy initialization.** `Context` creates its `UploadManager` on the first successful
`createUploadBatch()` request through `Context::Impl::ensureUploads()`. Creating a
buffer alone does not create that service. Before the first batch, Step 2 succeeds
without collection; afterwards it uses the existing manager to reclaim finished
uploads. The manager remains owned by `Context` even when no batches are pending.

**Completed uploads.** The shared [completed-upload subflow](reclaim-completed-uploads.md)
explains what collection releases, how it observes the queue timeline and why
resources with other owners remain alive.

**Memory selection.** `BufferDesc::memory` tells Panda which memory properties to
require and prefer. `Upload` and `Readback` require memory that Vulkan allows the
CPU to access. The returned `Buffer` is still an owning handle, not a CPU pointer;
Panda's current public API writes buffer data through `UploadBatch`.

**Safe updates.** If the GPU is still reading a buffer, overwriting its bytes can
change the data underneath that work. Updates must be ordered after the earlier
GPU reads. Choosing `Upload` or `Readback` does not establish that ordering; the
[resource synchronization contract](../design/contracts/resources.md#lifetime-is-not-synchronization)
defines this separate obligation.

| Memory intent | Required property | Preferred property          |
|---------------|-------------------|-----------------------------|
| `Device`      | Device-local      | None beyond the requirement |
| `Upload`      | Host-visible      | Host-coherent               |
| `Readback`    | Host-visible      | Host-cached                 |

A preferred property helps select memory; it is not an additional mandatory
capability. The actual type must also satisfy the native buffer's requirements.

## Ownership after success

```mermaid
flowchart TD
    C["Context / Impl"] -->|owns| P["MemoryPool"]
    C -->|owns| D["Vulkan device"]
    A["Application Buffer"] -->|shared owner| R["BufferResource"]
    B["Copied Buffer"] -->|shared owner of the same state| R
    R -->|owns| V["Native buffer owner"]
    R -->|owns| M["MemoryAllocation"]
    R -.->|borrows| P
    M -.->|borrows pool ; owns a reserved range by id|P
P -->|owns|K["MemoryBlock objects and device-memory allocations"]
```

Copying a `Buffer` retains the same state rather than allocating or copying buffer bytes.
`MemoryAllocation` owns a range reservation, not the whole pool block. It releases
that reservation through its borrowed pool when destroyed.

**Lifetime requirement.** No owning edge links `Buffer` to `Context`. `Context` must outlive all these handles;
moving `Context` preserves its stable internal services. Releasing one `Buffer` only
drops one shared owner. Once no owner remains, `BufferResource` destroys the native
buffer before releasing its range. Submitted uses can retain additional owners;
their retention is outside this creation flow and follows the
[resource lifetime contract](../design/contracts/resources.md#frame-contexts-and-retirement).

## Variants and errors

|  | Point and condition                          | Response                                                                                                           | Resulting state and next action                                                                                                         |
|--------|----------------------------------------------|--------------------------------------------------------------------------------------------------------------------|-----------------------------------------------------------------------------------------------------------------------------------------|
| **Error&nbsp;1** | Step 1: moved-from `Context`                     | Checked fatal diagnostic                                                                                           | No recoverable return; use a live `Context`                                                                                             |
| **Error&nbsp;2** | Step 2: upload collection fails                  | Propagate its error before attempting resource creation                                                            | No new buffer; follow the error's recovery category, not unconditional retry                                                            |
| **Error&nbsp;3** | Step 3: invalid size, usage or memory intent | Return `InvalidArgument` or `Unsupported`                                                                          | No new native buffer; correct size, usage or intent                                                                                     |
| **Error&nbsp;4** | Step 4: native creation fails                    | Convert native status to Panda `Error`; do not adopt failed output                                                 | No published resource; inspect error category and native diagnostics                                                                    |
| **Error&nbsp;5** | Step 5: allocation fails                         | Destroy the unpublished native buffer and propagate allocation error                                               | No new handle or retained range for this request; existing resources remain owned; retry only if the cause is resolved                  |
| **Error&nbsp;6** | Step 6: binding fails                            | [Destroy the buffer, then release its allocation](#error-6---cleanup-after-binding-failure); propagate the native error | No published handle; memory-pool cache/accounting need not return to their exact starting values; further use depends on error category |

**Errors 2–6** return errors through `Result`; that does not mean every category permits
recovery on the same `Context`. **Error 1 is fatal.** Catastrophic host allocation failure
follows the [shared error policy](../design/contracts/resources.md#errors).

### Error 6 - Cleanup after binding failure

**Entry state.** Steps 1–5 completed. The unpublished `BufferResource` owns a native buffer and a
`MemoryAllocation`. The bind call at Step 6 failed. No work was submitted by this operation
and no public `Buffer` exists. This path needs no GPU completion wait.

```mermaid
sequenceDiagram
    participant A as Application
    participant C as Context / Impl
    participant R as BufferResource
    participant M as MemoryAllocation
    participant P as MemoryPool
    participant V as Vulkan device
    V -->> R: Error 6 - bindBufferMemory2 failed
    Note over R: bind() returns an error, create() releases unpublished state
    R ->> V: Destroy native buffer
    R ->> M: Release allocation owner
    M ->> P: release(id)
    P -->> M: Range released, empty-block policy applied
    R -->> C: Error, no shared resource published
    C -->> A: Result containing Error
```

Returning a range does not always free a whole device
allocation: the pool may retain an empty ordinary block; dedicated blocks are
released when empty. The native error category/status survives propagation.

## Outcomes

**Success.** A copyable `Buffer` owns fixed-size, memory-bound storage with **undefined contents**. No completion token
is needed for creation because no work was
submitted. The application must initialize contents before reads and release every
owner before `Context` destruction.

**Failure.** Recoverable creation failure publishes no handle and releases temporary resource
owners. It does not roll back unrelated completed-upload collection or promise
byte-for-byte restoration of pool caches, nor recover a lost device.

## Related information

- [Ownership](../design/contracts/resources.md#ownership),
  [lifetime versus synchronization](../design/contracts/resources.md#lifetime-is-not-synchronization),
  and [handle boundary](../design/contracts/resources.md#handles-and-api-boundary)
  define the shared guarantees.
- [Reclaim completed upload packets](reclaim-completed-uploads.md) explains the
  shared collection performed before this allocation.
- [Vulkan memory allocation](https://docs.vulkan.org/guide/latest/memory_allocation.html)
  explains device-memory allocation and why resources can share larger allocations.
- [vkBindBufferMemory2](https://docs.vulkan.org/refpages/latest/refpages/source/vkBindBufferMemory2.html)
  specifies native memory binding, including compatible memory types, size and offset
  requirements. Panda's Step 5 must obtain a range satisfying those requirements before Step 6.
- [vkDestroyBuffer](https://docs.vulkan.org/refpages/latest/refpages/source/vkDestroyBuffer.html)
  specifies native destruction and the requirement to finish submitted uses first.
  This creation flow submits no work; later users must satisfy that lifetime rule.
