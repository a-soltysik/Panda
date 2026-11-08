# Render and present an application view

## Goal and boundary

An application passes camera, lighting and mesh draw data to `Context::render(view)`.
Panda renders and presents that view without requiring a scene or entity system. An
empty `RenderView::meshes` list is a valid view that presents only the background.
This flow ends with a `FrameResult` or an `Error`.

**The application** owns scene data, transforms and the lifetime of its `Context`.
**Panda** validates the view, records the built-in mesh rendering path, retains mesh
and selected texture handles through GPU use, and owns frame-slot and swapchain synchronization. A returned
`CompletionPoint` identifies submitted GPU work; it does not mean the image has been displayed.

## Preconditions

1. A live windowed `Context` exists. Calls to it are serialized on its execution thread.
2. Every mesh draw refers to a valid, uploaded mesh from that Context and declares a
   layout compatible with its material. All draws require Position; Lit also requires
   Normal. Color is optional and defaults to white. A base-color map is optional; if
   set, it must be a sampled RGBA8 sRGB texture from the same Context, its upload must
   have been submitted, and the mesh must declare TexCoord0.
3. Camera, light, transform, tint and material values are finite and valid for the
   built-in renderer. A zero-sized framebuffer is allowed and returns `Suspended`.

## Main flow

1. **Build the view.** The application supplies a view-projection matrix, camera
   position, one directional light and zero or more mesh draws with per-mesh materials.
   Mesh handles are ordinary values; Panda does not create entities or own application
   transforms.
   For position, rotation and scale, `panda::createModelMatrix()` builds the full
   object-to-world matrix; applications may also supply a matrix directly.
2. **Validate and collect uploads.** `Context::Impl::render()` checks matrices, light,
   tint, material alpha mode and cutoff, then verifies each mesh's Context and required attributes.
   If a base-color map is set, it also checks the texture's Context, sampled usage,
   sRGB format and the mesh's TexCoord0. It polls and reclaims completed upload packets,
   then rejects a texture whose upload was never submitted. Rendering remains queued
   after any in-flight upload on the Context submission queue.
3. **Prepare the window frame.** `Presentation::prepareWindowFrame()` checks the
   framebuffer extent and swapchain generation. A zero extent returns `Suspended`
   without acquiring an image. See [UC Suspend presentation and recreate the
   swapchain](recreate-swapchain.md).
4. **Retain resources and acquire.** `Presentation` waits for the selected frame
   slot's prior GPU completion, then retains the view's mesh and selected texture handles in that slot. It
   checks submission capacity, acquires an image and waits for that image's pending
   presentation fence before reuse.
5. **Record the view.** An empty mesh list clears the acquired image to the configured
   background. Otherwise, `Presentation` creates or resizes the internal `MeshRenderer`,
   updates frame data, per-draw material records and texture descriptors, then records
   the directional shadow depth pass. A base-color sample multiplies material and
   draw tint and optional vertex color; an absent map contributes white. Opaque draws
   cast a full silhouette. Mask draws multiply texture, material, draw and vertex alpha
   and apply the material cutoff in both shadow and camera passes. The camera pass
   selects pipelines from each material model, alpha mode, consumed vertex attributes,
   face mode and transform handedness while sampling the shadow map for Lit draws.
6. **Submit GPU work.** `SubmissionQueue` submits the command buffer, waiting on the
   acquire semaphore and signaling the image's render-finished semaphore plus the
   Context timeline. Successful submission commits its timeline value, stores it on
   the frame slot and advances the slot. Mesh and texture handles remain retained until that slot
   is reused after completion. See [UC Commit work to the Context submission
   timeline](commit-context-submission.md).
7. **Queue presentation.** `Presentation::presentFrame()` queues the image with its
   per-image presentation fence. Normal presentation returns `Presented`; a
   suboptimal/out-of-date result returns `RecreatePending` with the accepted submission's
   completion point. Other failures follow [UC Recover from a presentation failure
   after GPU submission](recover-presentation-failure.md).

```mermaid
sequenceDiagram
    participant A as Application
    participant C as Context / Impl
    participant U as UploadManager
    participant P as Presentation
    participant R as MeshRenderer
    participant Q as SubmissionQueue
    participant V as Vulkan
    A->>C: render(view)
    C->>C: Validate camera, light, mesh layouts and texture requirements
    opt UploadManager exists
        C->>U: Collect completed upload packets
        U-->>C: Collection result
    end
    C->>C: Reject base maps without a submitted upload
    C->>P: Render validated view
    P->>V: Check framebuffer and swapchain generation
    P->>P: Wait for frame slot and retain mesh and selected texture handles
    P->>V: Acquire image and wait for presentation reuse
    alt View has no meshes
        P->>V: Clear background with dynamic rendering
    else View contains meshes
        P->>R: Create/resize renderer and update frame, material and texture data
        R->>V: Record Mask shadow cutouts using the same base-color alpha as the camera
        R->>V: Record Opaque and Mask draws, then back-to-front Blend draws
        R->>V: Sample shadow map for Lit draws
    end
    P->>Q: Submit commands and signal timeline
    Q-)V: Queue accepts frame work
    Q-->>P: Completion value
    P->>V: Present image and signal its present fence
    V-->>P: Present outcome
    P-->>C: FrameResult or Error
    C-->>A: Result<FrameResult>
```

## Notes

**No scene requirement.** Applications can prepare `RenderView` data from their own
architecture. An optional scene library may prepare the same render data, but Core
does not depend on that library.

**Camera construction.** `panda::tools::createPerspectiveCamera()` combines the
checked perspective projection with right-handed look-at vectors and returns a
`RenderCamera` for the view.

**Current shading model.** Built-in materials multiply linear `baseColor`, an
optional sRGB base-color map, draw tint and optional vertex color (white when absent).
Mapped draws require TexCoord0; a missing map contributes white. Lit materials evaluate one directional
light with a Cook-Torrance microfacet specular term: GGX normal distribution,
Schlick-GGX geometry and Schlick Fresnel with dielectric F0 0.04. Lambert diffuse is
combined with that specular term. Each Lit material supplies metallic and roughness,
clamped to the supported range. Unlit materials skip light evaluation and shadow
lookup while shading, but still cast shadows in the current depth pass.
`AlphaMode::Opaque` renders the whole surface without alpha blending.
`AlphaMode::Mask` turns alpha into a binary cutout: values below `alphaCutoff` are
discarded, and the rest render fully opaque. The same cutoff controls the shadow
silhouette. The material's `RenderFace` selects front sides, back sides or both; transforms with
a negative determinant reverse pipeline winding in both passes. Lit shading
orients back-facing normals consistently when rendering `Back` or `Both`. They
use base color, not emissive lighting. A constant ambient term contributes
0.06 times base color to Lit materials. The shader applies the ACES fitted tone curve and
outputs sRGB, using hardware conversion for sRGB swapchain formats or shader encoding
for UNORM formats. Direct directional light is shadowed with one 2048² depth map per
frame slot and 3x3 PCF; ambient light remains unshadowed. The map uses a 30 m
orthographic view centered 15 m ahead of the camera and a 0.1–80 m light-depth range.
Points outside that fixed camera-centered region receive no shadow. Material textures,
multiple lights, spot/point shadows and improved shadow coverage and filtering remain work in
[Materials, lights and shadows](../development/tasks/materials-and-shadows.md). The
current 3x3 PCF has visible edge aliasing; the first-scene task records that limit and
the current visual result. The first mesh draw selects the highest common color/depth
attachment sample count supported by the active formats and device limits (4x, then
2x, then 1x); `FrameResult::effectiveSampleCount` reports it.

`AlphaMode::Blend` uses the combined texture, material, draw and optional vertex alpha
for straight-alpha compositing. It runs after Opaque and Mask with depth testing enabled
and depth writes disabled; mesh origins are sorted back-to-front. Blended meshes do
not cast shadows. Intersecting transparent meshes can still sort incorrectly.

**Slot and image lifetimes differ.** Panda has two reusable frame slots while the
swapchain supplies its own image count. Command buffers and retained mesh handles
belong to slots; render-finished semaphores and presentation fences belong to images.

**Completion meanings.** The timeline point covers submitted GPU work. The per-image
present fence separately tracks presentation-resource retirement; it does not promise
that display has finished. See the
[presentation-fence reference](https://docs.vulkan.org/refpages/latest/refpages/source/VkSwapchainPresentFenceInfoKHR.html).

**Pre-submit failures.** After acquire succeeds, a recording or submit failure must
consume the acquire semaphore and retire the image. See [UC Retire an acquired image
after frame work fails](retire-acquired-image.md). A frame error does not imply that
already submitted work was rolled back.

## Variants and errors

| Condition                                                        | Result and next action                                                                       |
|------------------------------------------------------------------|----------------------------------------------------------------------------------------------|
| Framebuffer width or height is zero                              | Return `Suspended` without acquiring an image; process events and retry after restoration.   |
| Acquire returns timeout or not-ready                             | Return `Skipped` without a completion point.                                                 |
| Acquire reports out-of-date                                      | Return `RecreatePending` without a completion point; the next call attempts recreation.      |
| Acquire or present is suboptimal, or present reports out-of-date | Return `RecreatePending`; keep the accepted submission's completion point.                   |
| Invalid view/material data, foreign mesh/texture, incompatible layout, or invalid base-color map | Return `InvalidArgument` before acquiring an image. |
| Context has no window presentation service                       | Return `Unsupported`.                                                                        |
| Recording, submission or cleanup fails after acquire             | Return the relevant error; retire the acquired image. Failed retirement faults presentation. |
| Surface or device is lost                                        | Return the Vulkan error and fault presentation; Context teardown still attempts its drain.   |

## Related information

- [Rendering contract](../design/contracts/rendering.md) defines the renderer boundary
  and its supported inputs.
- [Resources and frames](../design/contracts/resources.md#frame-and-window-state)
  defines frame results, recreation and presentation state.
- [UC Upload texture contents or regions](upload-texture.md) explains how a base-color
  map receives its image data before rendering.
- [UC Collect paired CPU and GPU frame timings](collect-frame-timings.md) describes
  optional timing markers around submitted view work.
- [UC Observe or wait for submitted GPU work](observe-completion.md) explains how an
  application observes a returned `CompletionPoint`.
