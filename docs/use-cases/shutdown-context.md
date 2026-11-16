# Shut down an Engine safely

## Goal and boundary

An application ends the lifetime of a live `Engine`. **The application** first
releases its `Buffer`, `Texture` and `Mesh` handles and destroys every
unsubmitted `UploadBatch`. **Panda** then drains its submitted upload packets
and presentation use for windowed Engines, offscreen execution, and external
handoff work when available. If those checks pass, the Engine's RAII owners tear
down its device services and native objects.

The flow starts when Engine is destroyed and ends with normal teardown or a fatal
diagnostic. Its out-of-line destructor releases the single implementation owner,
and `Engine::Impl` drains exactly once from its destructor. The destructors are
`noexcept`; teardown cannot return a
recoverable drain error or promise general recovery from device loss.

## Preconditions

1. **Caller-owned state is released.** Every public resource-handle copy and
   unsubmitted batch is gone before the Engine is destroyed. The borrowed
   `WindowSurface` and its native window remain alive through teardown.
2. **Serialized use.** No Engine operation is in progress while its owner is
   destroyed.

## Main flow

1. **Release application owners.** The application releases its resource handles
   and either submits or destroys each upload batch while the Engine is still
   alive. A submitted packet can continue to retain staging and destination
   resources after the application releases its handles.
2. **Begin destruction.** Engine leaves scope or is reset. Its destructor releases
   the single implementation owner, and `Engine::Impl::~Impl()` starts the drain
   before dependencies are released.
3. **Drain submitted uploads.** `Engine::Impl::drain()` first requires that no
   unsubmitted batches remain, then asks the Engine's `UploadManager` to wait up
   to ten seconds for pending uploads. With no pending packets, it returns immediately.
   [UC Reclaim completed upload packets](reclaim-completed-uploads.md) explains
   when the retained owners can be released.
4. **Drain presentation.** If this is a windowed Engine,
   `WindowPresentation::drain()` settles submitted frame and presentation use.
   [UC Drain frame and presentation use](drain-presentation.md) explains the
   required completion checks.
5. **Drain offscreen rendering.** `OffscreenRenderBackend::drain()` completes pending
   readback and execution use, then the implementation releases its retained
   offscreen owners.
6. **Drain external handoffs.** If external-buffer interop is available,
   `ExternalBufferInterop::drain()` completes its submitted ownership handoffs.
7. **Check resource owners.** The memory pool must report `liveBytes == 0`.
   This means all resource allocation owners are gone after internal upload,
   rendering and handoff packets release their references. [UC Return a resource memory range](release-memory-range.md)
   explains how final owners return their ranges.
8. **Release Engine services.** After the live-allocation check, `Engine::Impl`
   releases its facade objects and directly owned service implementations.
   `DeviceContext` then releases the logical device, physical-device wrapper,
   surface, instance and loader. The application still owns its `WindowSurface`.

```mermaid
sequenceDiagram
    participant A as Application
    participant H as Resource handles and upload batches
    participant C as Engine
    participant I as Engine::Impl
    participant D as DeviceContext
    participant U as UploadManager
    participant P as WindowPresentation
    participant O as OffscreenRenderBackend
    participant X as ExternalBufferInterop
    participant M as MemoryPool
    A->>H: Step 1 - release handles and submit or destroy batches
    A->>C: Step 2 - end Engine lifetime
    C->>I: Step 2 - destroy implementation
    I->>U: Step 3 - require no active batches and drain
    opt Pending upload packets
        Note over I,U: Step 3 - UC Reclaim completed upload packets
        U-->>I: Completed packets released, or drain error
    end
    U-->>I: Drained or error
    opt Windowed Engine
        I->>P: Step 4 - drain()
        Note over I,P: Step 4 - UC Drain frame and presentation use
        P-->>I: Drain succeeded
    end
    I->>O: Step 5 - drain and release offscreen rendering
    O-->>I: Drain succeeded
    opt External-buffer interop available
        I->>X: Step 6 - drain handoff packets
        X-->>I: Drain succeeded
    end
    I->>M: Step 7 - require liveBytes == 0
    M-->>I: No live allocation ranges
    I->>I: Step 8 - destroy service facades
    I->>I: Step 8 - release implementation owners
    I->>D: Step 8 - release Vulkan device data
    D-->>I: RAII teardown complete
    I-->>C: Implementation destruction complete
    C-->>A: Destructor returns
```

## Notes

**Caller and internal owners are different.** A copied handle still owns the
same resource state. A submitted `UploadCommand` is another owner of its
destination and staging resources until completion. Releasing the application's
copy alone does not make its memory range free. [UC Reclaim completed upload packets](reclaim-completed-uploads.md)
shows when the internal owner is released.

**Borrowed services follow the moved-to owner.** Moving Engine transfers its
stable implementation without relocating its service objects. References keep
their address and remain usable through the moved-to Engine, then expire when
that Engine is destroyed.

**The waits are targeted.** Upload draining waits for its last committed upload
value. WindowPresentation draining separately checks queue completion, image recovery
and present fences. These waits protect tracked uses; teardown does not use a
routine device-wide idle.

**The GPU completes the timeline value.** An earlier call to
`SubmissionQueue::submit()` has already accepted the work and committed its
signal value. The destructor path calls `SubmissionQueue::waitValue()` on the
calling thread; that wait returns immediately if completion is already known.
Otherwise the GPU queue signals the timeline after completing the submitted
work. Destruction does not submit more work or require another `Engine` call.

**The pool may have no live ranges and still own memory.** `liveBytes == 0` allows
the pool's cached empty ordinary blocks to remain until the pool itself is
destroyed. Dedicated blocks are released when their allocation range is
returned. See the [UC Return a resource memory range](release-memory-range.md).

## Variants and errors

|                    | Point and condition                                                                                                     | Response                                              | Resulting state and next action                                                                                                    |
|--------------------|-------------------------------------------------------------------------------------------------------------------------|-------------------------------------------------------|------------------------------------------------------------------------------------------------------------------------------------|
| **Variant&nbsp;1** | Step 3: no upload packets are pending                                                                                   | Upload drain returns without waiting                  | Continue to presentation drain                                                                                                      |
| **Variant&nbsp;2** | Step 4: headless Engine has no presentation service                                                                     | Skip presentation drain                               | Continue to offscreen drain                                                                                                         |
| **Variant&nbsp;3** | Step 5: offscreen execution was never initialized                                                                       | Drain succeeds without Vulkan work                    | Continue to external handoff drain                                                                                                  |
| **Variant&nbsp;4** | Step 6: external-buffer interop is unavailable                                                                          | Skip handoff drain                                    | Continue to the resource-owner check                                                                                                |
| **Error&nbsp;1**   | Step 3: an unsubmitted batch is still alive                                                                             | Fatal lifetime diagnostic before upload waiting       | Engine destruction cannot return; destroy or submit batches before Engine                                                        |
| **Error&nbsp;2**   | Step 3: the upload completion wait times out or fails, including device loss                                            | `expect` emits a fatal diagnostic                     | Pending owners are not treated as completed; teardown stops                                                                        |
| **Error&nbsp;3**   | Step 4: a timeline or present-fence wait fails, or acquired-image recovery is unsafe (including device loss)            | `expect` emits a fatal diagnostic                     | WindowPresentation resources are not claimed safe; teardown stops. See the [UC Drain frame and presentation use](drain-presentation.md) |
| **Error&nbsp;4**   | Step 5: offscreen execution or readback cannot drain                                                                     | `expect` emits a fatal diagnostic                     | Offscreen resources are not claimed safe; teardown stops                                                                           |
| **Error&nbsp;5**   | Step 6: an external handoff wait fails, including device loss                                                           | `expect` emits a fatal diagnostic                     | External ownership is not claimed returned; teardown stops                                                                         |
| **Error&nbsp;6**   | Step 7: `liveBytes` is nonzero after internal drains                                                                    | Fatal lifetime diagnostic naming live resource owners | Release every external handle before destroying Engine; no recoverable Engine remains                                            |

## Outcomes

**Normal teardown.** Upload, presentation, offscreen and external handoff uses
are confirmed complete, no external resource allocation owners remain, and
Engine's RAII owners release their native state. The pool may still own empty
cached blocks until this step.

**Fatal teardown.** A live batch, unresolved GPU, presentation, offscreen or
external handoff work, or live resource allocation violates the shutdown
boundary. Since the destructor cannot return an `Error`, Panda stops with a fatal
diagnostic instead of claiming that unsafe resources were released. This is not
a general device-loss recovery path.

## Related information

- [Ownership](../design/contracts/resources.md#ownership) defines which owners
  must outlive Engine and the caller's release responsibility.
- [Frame contexts and retirement](../design/contracts/resources.md#frame-contexts-and-retirement)
  defines when retained resource uses may be released.
- [Allocation, uploads and descriptors](../design/contracts/resources.md#allocation-uploads-and-descriptors)
  defines upload retention and pool ownership.
- [UC Reclaim completed upload packets](reclaim-completed-uploads.md),
  [UC Drain frame and presentation use](drain-presentation.md) and
  [UC Return a resource memory range](release-memory-range.md) explain the
  upload, presentation and allocation-release boundaries used here.
- [Allocation, uploads and resource lifetimes](../development/tasks/resource-lifetimes.md)
  and [Vulkan context and window lifecycle](../development/tasks/vulkan-context.md)
  record the implemented lifetime and teardown scope.
