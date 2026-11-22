# Render a view to an image

## Goal and boundary

An application renders a `RenderView` into an Engine-owned `OffscreenTarget` with
`OffscreenRenderer::render()`, then optionally calls `readPixels()` to obtain RGBA
bytes. The render call submits GPU work without presenting a window frame. Reading
pixels is a separate operation that waits for its copy to finish.

**The application** supplies the view, chooses a target per render, and keeps the
owning `Engine` alive. **Panda** validates ownership and view resources, retains
them through GPU completion, and defines the readback layout. A target belongs to
the Engine that created it.

## Main flow

1. **Create the target.** The application calls
   `Engine::getResourceManager().createOffscreenTarget()` with a nonzero
   `glm::uvec2` extent in pixels (x is width, y is height). The current target
   format is RGBA8 sRGB.
2. **Build and upload the view.** The application supplies camera, lights, an
   optional skybox and mesh draws. A skybox is an uploaded, sampled square linear
   `Rgba16Float` cubemap from the same Engine. Required mesh and texture data must be uploaded; see
   [UC Upload a complete mesh payload into a batch](upload-mesh-payload.md) and
   [UC Upload texture contents or regions](upload-texture.md).
3. **Submit the render.** The application calls
   `Engine::getOffscreenRenderer().render(view, target)`. `OffscreenRenderer`
   validates the view and target, collects completed uploads, then lazily initializes
   `OffscreenRenderBackend` before recording the built-in rendering path. It
   prepares the shared HDR renderer and SDR output pipeline for mesh draws or a skybox,
   retains the target and view resources,
   then submits the command buffer on
   the Engine queue. Success returns `FrameResult::Status::Rendered`, a
   `CompletionPoint` and the effective sample count. A skybox-only view uses one
   sample; mesh draws follow `RenderView::requestedSampleCount`.
4. **Request pixels.** When CPU pixels are needed, the application calls
   `readPixels(target, completion)` with a point covering that target's latest
   render. Panda validates the target and point, submits an image-to-buffer copy,
   waits for that copy, and returns tightly packed `ImageReadback` bytes.

```mermaid
sequenceDiagram
    participant A as Application
    participant M as ResourceManager
    participant R as OffscreenRenderer
    participant U as UploadManager
    participant O as OffscreenRenderBackend
    participant E as FrameExecution
    participant Q as SubmissionQueue
    participant V as Vulkan
    A->>M: Step 1 - createOffscreenTarget(extent)
    M-->>A: OffscreenTarget
    A->>A: Step 2 - build and upload view
    A->>R: Step 3 - render(view, target)
    R->>R: Validate view and target
    R->>U: Collect completed upload packets
    U-->>R: Collection result
    R->>R: Reject material maps or skybox without a submitted upload
    R->>O: Initialize lazily and submit offscreen frame
    O->>E: Prepare HDR attachments and target-format output pipeline when needed
    O->>E: Retain target and view resources and record content
    alt No meshes and no skybox
        E->>V: Transition target and clear to the transformed solid background
    else Skybox or mesh draws
        Note over E,V: UC Render a view through the shared HDR path writes SDR pixels to the target
    end
    O->>Q: Submit command buffer
    Q-->>O: CompletionPoint
    O-->>R: FrameResult::Rendered
    R-->>A: FrameResult
    A->>R: Step 4 - readPixels(target, completion)
    R->>R: Validate target ownership and render state
    R->>O: Copy target to host-visible buffer
    O->>Q: Submit copy and wait for completion
    Q-->>O: Copy completed
    O-->>A: ImageReadback
```

## Notes

**Output contract.** Pixels are top-to-bottom rows in RGBA order. RGB channels are
sRGB encoded, alpha is linear UNORM, and rows have no extra padding.
`ImageReadback::extent` uses the same x-width/y-height pixel order. Image-file
encoding is a separate `Panda::Tools` operation; see
[UC Write an image file](write-image-file.md).

**GPU work and readback.** Rendering writes a GPU image and does not copy it to host
memory. `readPixels()` performs that copy only when requested, then waits so the
returned vector is ready for CPU use. This distinction keeps ordinary rendering
asynchronous.

**Target reuse.** Each render names its target, so one renderer can target different
images across calls. The supplied completion must cover the latest render of the
chosen target; submitted work retains the target until it is safe to release. See
the [resource lifetime contract](../design/contracts/resources.md#frame-and-window-state).

**Sample count.** Request a count through `RenderView` and query supported choices
with `OffscreenRenderer::getSupportedSampleCounts()`. Multisampled mesh color is
resolved into a single-sample HDR image before tone mapping writes the SDR target
for readback or window capture. See
[UC Render a view through the shared HDR path](render-hdr-output.md).

**Window capture.** A windowed Engine can use
`WindowRenderer::renderWithCapture(view, target)` to render once and write
format-correct SDR output to the swapchain and target. See [UC Render and present an application
view](render-view.md).

## Variants and errors

| Condition | Result and next action |
| --- | --- |
| Target extent is zero | Return `InvalidArgument`; create a nonzero target. |
| Device does not support the fixed RGBA8 sRGB target usage | Return `Unsupported`; use a compatible device. |
| Target or view resource belongs to another Engine | Return `InvalidArgument` before submission; create or upload it on the owning Engine. |
| Target has not been rendered, or completion is stale/foreign | `readPixels()` returns `InvalidArgument`; render to this target and pass a point that covers its latest render. |
| GPU submission, allocation or readback wait fails | Return the backend error; no pixels are returned. |

## Related information

- [Rendering contract](../design/contracts/rendering.md#offscreen-output) defines
  the shared rendering and image boundary.
- [UC Render a view through the shared HDR path](render-hdr-output.md) explains
  how the target receives tone-mapped SDR pixels.
- [Frame and window state](../design/contracts/resources.md#frame-and-window-state)
  defines Engine-scoped completion and resource retention.
- [UC Observe or wait for submitted GPU work](observe-completion.md) explains the
  completion point returned by rendering.
- [Offscreen rendering and image readback](../development/tasks/offscreen-rendering.md)
  records the implementation scope and evidence.
