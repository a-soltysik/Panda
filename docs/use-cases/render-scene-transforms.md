# Prepare a render view from scene objects

## Goal and boundary

An application asks optional `Panda::Scene` to prepare a `RenderView` containing its
visible meshes, active camera and directional light. **Panda::Scene** starts empty,
owns runtime identities, transforms and component values, and retains associated
mesh handles. **The application** creates entities, attaches camera/light
components, creates/uploads GPU assets, selects the camera, updates the scene and
owns the frame loop. This flow ends with a complete snapshot
or an `Error`; rendering follows
[UC Render and present an application view](render-view.md).

The [scene demo](../../examples/scene_demo/README.md) shows this path.
The [simple scene example](../../examples/simple_scene/README.md) shows direct
renderer use without Scene. Persistence remains in the
[scene persistence task](../development/tasks/scene-persistence.md).

## Preconditions

1. A live Context remains at the same address and outlives Scene and retained meshes.
2. The application has created a camera entity, attached a perspective or
   orthographic lens, selected it, and optionally created light entities. It has
   attached uploaded meshes to renderable objects. Mesh upload completes before rendering.

## Main flow

1. **Request a snapshot.** The application calls `Scene::createRenderView()` with the
   current framebuffer aspect ratio. Scene checks that its Context remains valid.
2. **Prepare the camera.** `SceneState` requires an active camera, updates its dirty
   world transform and builds the lens's view-projection matrix. Its world pose
   must have positive uniform scale and no shear; the lens ignores uniform scale.
3. **Collect visible objects.** `SceneState` updates required world caches, copies
   visible mesh transforms/tints and retains their mesh handles. The light's world
   rotation maps its local -Z axis into the lighting direction. Visibility applies
   to each object independently.
4. **Publish the snapshot.** Scene returns a value independent of subsequent scene
   edits. The application passes it to `Context::render()` in its frame loop.
   Context performs renderer data and mesh-ownership validation before submission.

```mermaid
sequenceDiagram
    participant A as Application
    participant S as Scene
    participant D as SceneState
    participant C as Context
    A->>S: Step 1 - createRenderView(aspectRatio)
    S->>C: Check live Context
    S->>D: Step 2 - Prepare active camera
    D->>D: Update ancestors and validate camera pose/lens
    D->>D: Step 3 - Collect visible meshes and light
    D-->>S: Camera, light and retained mesh draws
    S-->>A: Step 4 - Result containing snapshot
    A->>C: render(snapshot)
    C->>C: Validate renderer data and mesh ownership
```

## Notes

**Transform authority.** Local TRS remains authoritative. Parent changes invalidate
descendant caches; querying the view updates dirty ancestors before their children.
Mesh matrices preserve derived shear. Camera and light poses reject it because their
orientation must describe an orthonormal basis.

**Snapshot lifetime.** A snapshot contains copies of matrices and light/camera
values plus retained mesh handles. Deleting an object releases its Scene-owned mesh
reference without invalidating an already prepared snapshot. Context outlives both.
The renderer retains submitted mesh uses through GPU completion.

**Current light boundary.** The renderer supports one visible directional light and
its shadow pass. Multiple visible lights report `Unsupported`. With no visible
light, direct intensity is zero; the renderer's existing ambient term still applies.
Deleting the active camera clears selection instead of choosing another object.

## Variants and errors

|                  | Point and condition                                                                | Response                 | Resulting state and next action                             |
|------------------|------------------------------------------------------------------------------------|--------------------------|-------------------------------------------------------------|
| **Error&nbsp;1** | Step 2: no selected camera, invalid lens/aspect, reflected/nonuniform/sheared pose | Return `InvalidArgument` | No snapshot is published; select or correct the camera.     |
| **Error&nbsp;2** | Step 3: multiple visible directional lights                                        | Return `Unsupported`     | No snapshot is published; hide or remove additional lights. |
| **Error&nbsp;3** | Step 3: invalid light pose or non-finite derived world transform                   | Return `InvalidArgument` | Logical scene state is unchanged; correct the object data.  |

## Outcomes

**Success.** The application receives prepared renderer input, with no GPU work yet
submitted. Scene edits affect the next snapshot. Renderer-specific validation, such
as rejecting a mesh from another Context, occurs in `Context::render()`.

**Failure.** No partial snapshot escapes. Updating derived caches does not change
the authoritative transforms or component values.

## Related information

- [Scene API and transforms](../design/contracts/scene.md#entities-and-transforms) owns hierarchy, TRS validation and
  shear rules.
- [UC Render and present an application view](render-view.md) explains submission and mesh retention.
- [Scene identities and transforms](../development/tasks/scene-transforms.md) tracks remaining work and verification.
- [Scene demo](../../examples/scene_demo/README.md) shows the complete application loop.
