# Upload bytes into a buffer

## Goal and boundary

An application copies a nonempty byte range into an existing buffer by creating
an `UploadBatch`, calling `UploadBatch::write(Buffer, offset, bytes)`, then
submitting the batch. This implemented flow ends when the Context's queue accepts
the copies and returns a `CompletionPoint`; that point does **not** mean the GPU
has finished them.

**Panda** validates the destination, snapshots the caller's bytes into staging,
records ordered copies and retains their resources through completion. **The
application** creates and owns the destination, chooses the ranges and observes
completion when it needs to know that the work has finished. This scenario covers
buffer-only writes; texture and mesh writes add layout and atomicity rules.

## Preconditions

1. **Context.** Use a live `Context` and serialize its operations on the Context
   execution thread.

## Main flow

1. **Create a batch.** The application calls `Context::createUploadBatch()`.
   Context lazily creates its upload manager and, when needed, the shared queue
   timeline. The returned move-only batch borrows Context, submits no work and
   performs no completion wait.
2. **Snapshot each write.** For every range, the application calls
   `UploadBatch::write(destination, offset, bytes)`. Panda rejects an empty byte
   span, a foreign or empty buffer, missing transfer-destination usage or a range
   outside the buffer. It then checks for completed packets through the
   [completed-upload subflow](reclaim-completed-uploads.md), copies the bytes into
   the batch's mapped staging page and flushes noncoherent memory. It reuses the
   current page when it fits or creates another page. Only after staging succeeds
   does the batch append the copy, retaining both the page and destination.
   Writes retain insertion order, including overlapping ranges. The source bytes
   can be changed or released after `write()` returns.
3. **Prepare the upload.** `UploadBatch::submit()` asks `UploadManager` to
   check completed packets, verify queue capacity and record the batch's copies
   and memory barriers in an `UploadCommand`. The packet owns the staging pages
   and destination resources needed by the queued work.
4. **Submit and publish completion.** `SubmissionQueue` submits the command
   on the Context's graphics/compute queue. Only after Vulkan accepts it does the
   queue commit the timeline value and return a `CompletionPoint`. Success
   consumes the public `UploadBatch`; Context retains the packet until a later
   collection observes GPU completion.

```mermaid
sequenceDiagram
    participant A as Application
    participant C as Context / Impl
    participant B as UploadBatch / Impl
    participant U as UploadManager
    participant R as BufferResource
    participant P as MemoryPool
    participant Q as SubmissionQueue
    participant K as UploadCommand
    participant V as Vulkan queue
    A->>C: Step 1 - createUploadBatch()
    opt Headless Context has not initialized submissions
        C->>Q: Initialize shared timeline
        Q-->>C: Ready
    end
    C->>U: Create upload service and batch
    U-->>C: Batch implementation borrowing Context
    C-->>A: UploadBatch
    loop One or more buffer writes
        A->>B: Step 2 - write(destination, offset, bytes)
        B->>U: Validate Context, usage and range
        U-->>B: Valid destination
        B->>U: Check and reclaim completed uploads
        U-->>B: Collection result
        alt Current staging page has room
            B->>R: Copy bytes into mapped page and flush
            R-->>B: Staged range
        else A new page is required
            B->>U: Create transfer-source staging buffer
            U->>R: Create mapped BufferResource
            R->>P: Allocate buffer memory
            P-->>R: Memory range
            R-->>B: Staging page
            B->>R: Copy bytes into mapped page and flush
            R-->>B: Staged range
        end
        B->>B: Append ordered copy and retain its owners
        B-->>A: Write accepted, source bytes may be released
    end
    A->>B: Step 3 - submit()
    B->>U: Prepare all queued writes
    U->>U: Check completed uploads and queue capacity
    U->>K: Record copies and memory barriers
    K-->>U: Recorded command retaining resources
    U->>Q: Submit command
    Q->>V: Step 4 - Submit and signal next timeline value
    V-->>Q: Queue accepted work
    Q-->>U: Committed value
    U-->>B: CompletionPoint
    B->>B: Consume writes and staging pages
    B-->>A: CompletionPoint, batch is empty
```

## Notes

**Snapshot and ordering.** A buffer write copies exactly the requested bytes;
buffer offsets and lengths have no artificial four-byte alignment requirement.
Each copy is appended only after validation and staging succeed. Overlapping
writes in one batch therefore apply in insertion order. The shared
[upload contract](../design/contracts/resources.md#allocation-uploads-and-descriptors)
defines the copy barriers and visibility to later queue consumers.

**Ownership.** Before submission, the batch owns its staging pages and retains
each destination while borrowing Context. Destroying it before submit cancels
the queued writes and releases those owners without submitting this batch's work.
After successful submission, Context's pending packet retains the command, pages
and destinations until collection observes completion. Releasing the
application's `Buffer` handle does not release a destination still retained by
that packet.

**Completion.** The returned point identifies committed work on the Context's
shared timeline. It is a copyable token and does not retain Context or resources;
the issuing Context must remain alive while the application observes it. Use
`Context::poll()` or `Context::wait()` to observe GPU completion when needed;
submission itself does not block for completion. Uploads use explicit barriers
around the copies, so queue order alone is not treated as a memory dependency.

**Lazy service.** `Context::createUploadBatch()` creates the upload service on
first use. Headless Contexts also initialize their shared submission timeline at
that point; windowed Contexts use the timeline already shared with presentation.

## Variants and failures

|  | Point and condition | Response | Resulting state and next action |
| --- | --- | --- | --- |
| **Error&nbsp;1** | Step 1: Context is moved-from or upload service/timeline initialization fails | Return `InvalidArgument` or the initialization error | No batch or upload work is published; use a live Context or resolve the reported backend/capability problem |
| **Error&nbsp;2** | Step 2: empty bytes, foreign/empty buffer, missing transfer-destination usage or out-of-range write | Return a validation error before appending the write | Earlier writes remain in the batch; correct the request or discard the batch |
| **Error&nbsp;3** | Step 2: completion query, staging allocation, mapped write or cache flush fails | Propagate the error without appending this write | Earlier writes remain available; retry only when the error category permits it |
| **Error&nbsp;4** | Step 3: submit is called with no writes | Return `InvalidArgument` | The batch remains valid; add a write or destroy it |
| **Error&nbsp;5** | Steps 3–4: capacity check, command preparation or queue submission fails | Return the error without a `CompletionPoint` | The batch retains its writes and pages for a permitted retry; device loss does not promise recovery |

## Outcomes

**Success.** The Context queue accepted every copy in the batch and issued a
completion point. The batch handle is consumed, but GPU completion remains
asynchronous. The application can enqueue later Context work in queue order or
poll/wait for the point when it needs an observed completion result.

**Cancellation.** Destroying a batch before successful submission cancels its
writes and releases unsubmitted staging and destination owners. Context must
outlive the batch; destroying Context with a live unsubmitted batch is a fatal
lifetime violation.

## Related information

- [Reclaim completed upload packets](reclaim-completed-uploads.md) explains when
  Context releases the owners retained by submitted uploads.
- [Resource ownership, lifetime and synchronization](../design/contracts/resources.md#ownership)
  and [allocation, uploads and descriptors](../design/contracts/resources.md#allocation-uploads-and-descriptors)
  define the shared resource and upload guarantees.
- [Allocation, uploads and resource lifetimes](../development/tasks/resource-lifetimes.md)
  records the implemented API boundary and accepted task evidence.
- [Vulkan synchronization examples](https://docs.vulkan.org/guide/latest/synchronization_examples.html)
  show how barriers establish dependencies between copies and later resource uses.
- [`vkCmdCopyBuffer`](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdCopyBuffer.html)
  specifies the Vulkan buffer-copy command used by the recorded upload packet.
