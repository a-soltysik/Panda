# Scene, assets, and tools contract

Accepted specification for scenes, transforms, persistence, import and tools.
API sketches describe the intended interface; implementation follows [the plan](../../development/PLAN.md).

## Useful scene defaults

The default scene setup creates an editable camera, one shadow-casting directional
light, simple ambient illumination, and a usable standard material. A ground helper
may be requested to make shadows immediately visible. The first cube should not
require constructing a custom lighting pipeline.

These are ordinary scene objects/settings, not hidden lights that cannot be edited
or saved. An explicit empty-scene mode omits the convenience setup. Scene objects
remain the normal public rendering interface.

## Entities and transforms

Entities compose data; they do not need to inherit from a base class with virtual
`update`, `render`, and `compute` methods. Application classes may hold entity and
resource handles and register callbacks.

The authoritative local transform is translation, a normalized quaternion, and
scale. Derive and cache world matrices when dependencies change. Quaternions express
rotation, not translation, nonuniform scale, or shear; matrices remain appropriate
for the derived affine transform used by rendering.

The intended conventions are right-handed coordinates, Y up, camera-local forward
along -Z, meters, seconds, and radians in APIs. GUI angles may be shown in degrees.
Projection/depth and public math conventions are specified below; the separate
shader ABI is specified in the rendering contract.

Hierarchy rules:

- An entity has at most one parent; reject cycles and cross-scene parenting.
- World transform is parent world multiplied by local transform.
- Local and world operations are explicitly named; callers do not manually update caches.
- Reparenting distinguishes keeping the local transform from keeping the world transform.
- Local storage remains TRS. A keep-world operation that cannot represent the result
  because of shear or a singular parent fails explicitly instead of approximating silently.
- Nonuniform parent scale combined with child rotation can produce a sheared world
  transform. Rendering must not pretend the resulting matrix is pure rotation and scale.

Negative scale is a reflection, not a request to render the inside of a cube. The
renderer handles winding parity and the appropriate normal transform. Use material
face selection for an inward-visible domain; see [rendering](rendering.md).
The singular-transform policy is below; do not divide by zero or silently
invent an inverse.

## Public scene boundary

Use a move-only `Scene` with stable internal storage, owned by the application and
bound to one `Context`. The context outlives it. Ordinary headers expose Panda
entities, descriptions and value snapshots, not an EnTT registry. Engine component
storage types stay private; public `Transform` is a value, not a mutable registry
component reference. Mutators validate and mark derived data dirty themselves.

Design signatures (not implemented declarations):

| Operation | Semantics |
| --- | --- |
| `Scene::create(Context&, SceneOptions = {}) -> Result<Scene>` | Studio defaults unless Empty is explicitly selected; all created resources belong to the same context |
| `Scene::create_entity(EntityDesc = {}) -> Result<Entity>` | Creates identity, name, local identity TRS, and no parent; no drawable/behavior base class |
| `Scene::add_primitive(Primitive, ObjectOptions = {}) -> Result<Entity>` | Convenience factory for a mesh entity; initial primitives: Cube, Plane, Sphere |
| `Scene::contains(Entity) -> bool` | Checks this scene's runtime identity and the entity generation |
| `Scene::destroy(Entity, DestroyMode) -> Result<void>` | Explicit Subtree or EntityOnly; EntityOnly rejects an entity with children |
| `Scene::local_transform(Entity) -> Result<Transform>` | Returns a value copy, never a long-lived pointer into component storage |
| `Scene::set_local_transform(Entity, Transform) -> Result<void>` | Validates and updates local TRS; failure leaves previous state unchanged |
| `Scene::world_matrix(Entity) -> Result<Mat4>` | Updates required caches and returns the full affine matrix, including shear |
| `Scene::set_world_transform(Entity, Transform) -> Result<void>` | Desired world TRS; derives local TRS or returns a representability/inverse error |
| `Scene::set_parent(Entity, optional<Entity>, ReparentMode) -> Result<void>` | KeepLocal or KeepWorld; no implicit transform approximation |
| `Scene::set_visible(Entity, bool) -> Result<void>` | Suppresses rendering of that entity; does not stop compute, physics, or delete children |
| `Scene::set_active_camera(Entity) -> Result<void>` | Requires a camera from this scene; no camera means a clear render preflight error |
| `Scene::object_id(Entity) -> Result<ObjectId>` / `Scene::find(ObjectId) -> optional<Entity>` | Stable application/persistence reference versus ordinary absence |
| `Context::render(Scene&) -> Result<FrameResult>` | Normal high-level route; checks scene/context/camera before GPU execution and follows the [frame protocol](resources.md#frame-and-window-state) |

`Entity` is a small non-owning value with scene-instance identity and an opaque
generation-aware runtime identifier. It has no scene pointer to dereference after
scene destruction; all resolving operations take a live Scene. Moving a Scene
preserves identity; replacing/loading a different scene does not. Reject stale and
cross-scene handles. Do not expose the raw ID as a persistent identifier.

Use a 64-bit EnTT entity type internally. Scene-instance counters and generation
exhaustion must not wrap into an old valid handle; reject exhaustion rather than
silently aliasing a stale reference. Implementation tests must exercise the guard
through a reduced test counter range, not billions of real operations.

`ObjectId` is an opaque 128-bit identifier, rendered as 32 hexadecimal digits in
scene files. Generate/check uniqueness within a document; it is not a secret
or security token. Explicit IDs may be supplied through EntityDesc for reproducible
C++ scenes, with duplicate rejection. A save/load preserves IDs, while an ordinary
new entity gets a new one. Document identity follows the persistence rules below;
cross-file merging is outside scope. ObjectId is not a global entity lookup service.
Names are editable labels, may repeat, and never substitute for IDs.

Entity deletion removes its application components too, but not arbitrary callbacks
that merely captured that entity. Such application callbacks must be unregistered
or handle absence explicitly. Deletion of the active camera clears the selection;
it does not secretly select another camera. GPU retention follows the
[resource lifetime rules](resources.md#frame-contexts-and-retirement).

## Native ECS extension

Provide `panda/Ecs.hpp` for applications that want EnTT directly:

```cpp
enum class NativeEntity : std::uint64_t;
using NativeRegistry = entt::basic_registry<NativeEntity>;

NativeRegistry& native_ecs(Scene&);
const NativeRegistry& native_ecs(const Scene&);
Result<NativeEntity> to_native(const Scene&, Entity);
Result<Entity> to_entity(const Scene&, NativeEntity);
```

The extension borrows the scene's existing registry, not a second registry or a
generic Panda component framework. The core target supplies the compatible EnTT
include dependency; `panda/Panda.hpp` and CUDA bridge headers do not include it.
Applications explicitly opting into this header also opt into the selected EnTT API.

Applications may add/query/remove their own component types and iterate native
views. Panda owns entity creation/destruction, hierarchy, transforms, renderable
state and other engine components. Do not call registry create/destroy/clear,
replace/move the registry, manipulate entity storage, or access private Panda
components through this extension. Use Scene methods for those operations. This
is a contractual native escape hatch, like borrowing a Vulkan command buffer;
it is not a claim that every forbidden EnTT operation can be blocked by its type.

Mutation is context-thread/update-phase only. During a view iteration, update values
of existing application components or call non-structural Scene setters; collect
entity/component additions and removals for after iteration. Do not keep component
references across structural changes, frames, or load/replacement. Panda does not
promise stable component addresses, a general event bus, or an ECS scheduler.
Use application functions/classes and explicit loops. Diagnostic builds should
detect unauthorized entity-lifecycle changes where feasible; they cannot repair them.

Custom components are not automatically saved, inspected by GUI, or copied to GPU.
An application can keep its domain data outside ECS instead; using EnTT is optional
for callers. No class inheritance, reflection registry, or user-component adapter
generator is required. The same extension supports a TirePressure component and a
single simulation-domain component without creating CPU entities for particles.

The library's entity versions, views and storage rules are documented by
[EnTT](https://github.com/skypjack/entt/wiki/Entity-Component-System).
The restricted native boundary above is Panda's policy, not an EnTT limitation.

## Transforms and camera conventions

Public math values use single-precision, packed GLM vectors/quaternions/matrices,
with aliases `Vec3`, `Vec4`, `Quat`, `Mat4` using explicit `glm::packed_highp`.
Require sizes 12/16/16/64 bytes and alignment 4 respectively in the pinned build.
GPU blocks have separate explicit packing; never memcpy a Transform as a shader
block or serialize raw GLM storage. The public quaternion convention is named
x/y/z/w fields; construction helpers avoid confusing GLM constructor argument order.

Do not force global GLM handedness/depth flags on a consumer. Use explicitly
right-handed, zero-to-one projection functions internally. Default packed aliases
do not solve arbitrary GLM ODR/configuration mismatch: Panda and its consumer must
use compatible GLM settings. Require both `GLM_FORCE_QUAT_DATA_WXYZ` and
`GLM_FORCE_QUAT_DATA_XYZW` to remain undefined and assert type layouts; the
external-consumer probe must cover include order and
configuration. Do not silently convert a differing binary ABI.
[GLM configuration](https://github.com/g-truc/glm/blob/master/manual.md) and
[explicit projection functions](https://github.com/g-truc/glm/blob/master/glm/ext/matrix_clip_space.hpp).

`Transform` stores position, rotation and scale; defaults are zero, identity and one.
Local matrix is T * R * S, world is parentWorld * local, and matrices multiply column
vectors. Reject non-finite fields; normalize a finite quaternion whose norm is at
least 1e-8, otherwise fail. Scale components must have absolute value at least 1e-6;
negative components are legal reflections. Use visibility to hide objects, not zero
scale. Non-finite derived world results fail validation before frame submission.

Compute world/normal matrices only when local or ancestor state changes. World
queries update necessary ancestors even before rendering; no public `update_matrix`
call is required. Keep-world reparenting computes inverse(newParentWorld) * oldWorld
and commits only if a local TRS reconstructs it. Detect shear using normalized basis
orthogonality (tolerance 1e-5) and require reconstruction residual at most 1e-5 relative
to matrix magnitude. For inverse validation, reject zero-length basis columns,
absolute determinant of the column-normalized basis at most 1e-8, non-finite results,
or max-element residual of A * inverse(A) against identity above 1e-4. Do not test
only determinant != 0. Put a negative
determinant's sign on the X scale for deterministic decomposition; quaternion sign
equivalence must not count as a changed physical orientation. Tolerances are testable
float-policy choices, not a promise of arbitrary numerical scale range.

The normal matrix is inverse-transpose of the world linear transform, not just its
rotation quaternion. Derived shear is allowed for ordinary mesh rendering; a world
operation requiring TRS fails when it cannot represent it. Gizmos/physics/physical
domains must report unsupported shear rather than silently dropping it. Their exact
shape restrictions are specified in the tools and physics contracts below.

Use conventional depth: near maps to 0, far to 1, clear depth 1, comparison LESS_OR_EQUAL.
Perspective has a finite positive near plane and far > near; initial camera values
are vertical FOV 60 degrees (API radians), near 0.05 m and far 1000 m. Orthographic
height is positive and clipping uses the same depth convention. Aspect follows the
render extent unless explicitly fixed. Apply the Vulkan Y correction once in the
projection matrix and use a positive-height viewport. Raster winding, shadow
projection and shader helpers must agree; no second hidden Y flip is allowed.
Reversed-Z, infinite-far projection and floating-origin worlds are outside version one.
[Vulkan depth convention](https://docs.vulkan.org/guide/latest/depth.html).

A camera's world basis must have positive uniform scale and no shear; use its
translation and orthonormal rotation for the view, ignoring that uniform scale for
the lens. Reject a reflected, nonuniformly scaled or sheared active camera during
render preflight rather than producing an accidentally distorted projection.
This restriction is narrower than ordinary mesh transforms. Light direction/shape
rules are specified in the rendering contract.

## First useful scene

`SceneOptions` selects Studio (default) or Empty and an optional studio ground.
Studio adds ordinary named camera/light entities, selects the camera, and installs
ambient/default material settings. Camera pose starts at (4, 3, 6), looking at the
origin with +Y up. The [renderer specification](rendering.md#first-version-renderer-contract)
defines lighting/material numeric defaults. Empty adds no entities or illumination;
convenience primitive creation can still use the standard material resource.

Cube is centered at the origin with side length 1 m. Plane lies in XZ, faces +Y and
has side length 1 m. Sphere has radius 0.5 m; tessellation is part of the primitive
mesh/benchmark fixture definition, not a parameter rebuilt each frame. Optional
studio ground is a 10 m plane at y=-0.5 m, so a default centered cube rests on it.
Objects share cached primitive meshes; entities and transforms remain independent.
The default material is scene-owned and shared by its default objects; clone before
private edits. An inward-visible domain uses the already agreed Back face state.

Illustrative API sketch, **not executable code in this repository**. `expect` denotes
the release-active checked fatal helper for a tiny demonstration; reusable application
code can inspect Result instead. ObjectOptions defaults are omitted for readability.

```cpp
auto context = panda::expect(panda::Context::create({.title = "Panda cube"}));
auto scene = panda::expect(panda::Scene::create(context, {.ground = true}));
auto cube = panda::expect(scene.add_primitive(panda::Primitive::Cube));

auto local = panda::expect(scene.local_transform(cube));
local.position.x = 1.0f;
panda::expect(scene.set_local_transform(cube, local));

while (context.poll_events()) {
    panda::expect(context.render(scene));
}
// Scene and its resource handles die before Context.
```

`poll_events()` processes window input and returns false when close is requested;
it does not secretly step physics/compute. `render()` returns a submitted completion
point or an explicit skipped-frame status from the frame protocol. The application
owns the loop; no required App subclass or game lifecycle callbacks are introduced.
Scene is the normal route, not a requirement to tie every native resource/compute
stage to an entity; the lower-level draw-data boundary is specified in the rendering
contract.

An application component uses the optional extension without an engine fork:

```cpp
struct TirePressure { float bar = 2.2f; };
auto& registry = panda::native_ecs(scene);  // requires panda/Ecs.hpp
registry.emplace<TirePressure>(panda::expect(panda::to_native(scene, cube)));
for (auto [id, pressure] : registry.view<TirePressure>().each()) {
    // Application logic; serialize/configure this data in the application.
    // Use to_entity(scene, id) and Scene setters for graphical changes.
}
```

Scene API verification: stale/cross-scene handles, duplicate stable IDs, scene moves,
parent cycles, subtree deletion, cached world updates, negative/nonuniform scale,
keep-world shear rejection and failure atomicity, invalid floats/quaternions/scale,
projection near/far/Y convention, custom-component cleanup, and a default lit-cube
integration example. CPU tests establish algebra/lifecycle, not visual GPU correctness.

## GPU clouds and simulation domains

A particle cloud is one logical renderable with GPU data, an instance count, and a
conservative bound. Its data layout need not contain a matrix for every instance;
positions, radii, and colors can be sufficient for billboards.

The domain and particles are distinct objects. Moving or rotating a physical domain
does not automatically parent or teleport the particles. The application solver
uses the domain's previous/current pose and extent changes to compute wall motion
and physical effects such as pressure. Panda renders the result and provides editing
and scheduling boundaries; it does not implement those equations.

Moving an entire visualization in local space is also valid, but is a separate
operation from moving physical boundaries through a simulation. Configuration and
tools must make that distinction clear. Buffer count changes or solver resets are
explicit apply/reinitialize operations at a safe update boundary.

## Tools

The optional tools module provides scene selection, transform inspection/gizmos,
camera/light/material controls, and direct application-defined parameter panels.
It should not require a reflection system or serialization of every C++ class.

Input capture prevents a UI drag from also controlling the scene camera. Tools emit
intents applied during the allowed update phase. They do not modify buffers already
in use by compute or rendering. Physics-sensitive edits follow the body's authority
rules in the [physics contract](physics.md).

## Import and asset references

Import is an optional adapter that normalizes supported source data into Panda
meshes, materials, textures, and scene nodes. Vulkan does not parse glTF or OBJ.

Prefer glTF/GLB for interchange. Plan runtime support for static OBJ/MTL and selected
Assimp-supported formats such as FBX, STL, and PLY, with a tested subset rather than
a claim that every source feature is supported. Report unsupported features and
lossy conversion instead of silently presenting a perfect-import claim.

Start with a blocking import API. An asynchronous asset manager, cooked-asset
database, and streaming system are not required for the first version. Built-in
primitives and shaders must not depend on the importer.

Use explicit asset roots and portable relative references rather than process
working-directory assumptions. Texture caching accounts for usage/color space;
the same source file used as color and as linear data cannot be conflated blindly.
Retaining CPU mesh data is a documented option, not a hidden permanent duplicate.
Importer mapping and path validation policy are specified below.

## Persistence boundary

Panda owns the scene format and serializes the state it understands: stable scene
IDs, hierarchy, transforms, renderable asset references, cameras, lights, and renderer
settings. The application does not implement Panda's internal scene serializer.

Application/domain configuration remains application-owned: solver type, time step,
pressure parameters, seeds, rain settings, and similar values. It may refer to scene
objects by stable IDs. The application-configuration boundary and load order are
specified below.

A saved application configuration may say that rain is enabled, identify a scene
object used as the simulation domain, and specify a seed. The Panda scene document
stores that object's graphics state, not rain or solver parameters. Saving the Panda
scene neither enumerates raindrops nor reads back GPU simulation state; an application
may define its own checkpoint separately.
A full simulation checkpoint would be a separate application feature.

The persistence section below specifies schema versioning, asset references,
unknown/unsupported data handling, missing resources, validation, atomic save
behavior, and scene/config load order. Do not serialize arbitrary
ECS memory or pretend GPU buffers are portable scene data.

## Persistence, import and tools

### Scene documents and application configuration

Use UTF-8 JSON through the private nlohmann/json adapter. Version-one root fields
are `schema:"panda.scene"`, `version:1`, `documentId` (32 lowercase hex digits),
`settings`, `materials`, and `objects`. Each object has an `id`, editable `name`,
optional `parentId`, local TRS (`position:[x,y,z]`, `rotation:[x,y,z,w]`, `scale:[x,y,z]`),
visibility and a component object with known typed camera/light/mesh/cloud entries.
`settings` contains ambient, active camera ID, requested renderer quality and
background. IDs are unique within the document; names may repeat. Emit stable key
ordering and finite numeric values for reviewable diffs. Serialize only accepted
scene fields; never dump EnTT storage, native handles or raw GLM bytes.

Mesh components refer to either a built-in primitive key or a logical asset ID.
`materials` stores the Panda-owned description of each scene-owned material once,
under a stable document-local material ID. The Studio material is registered by
default; `Scene::register_material(Material) -> Result<SceneMaterialId>` explicitly
registers another same-context material for scene persistence and returns its stable
ID. An object's material reference is tagged as either `scene` plus that ID or
`asset` plus an application logical ID; it cannot use both. The description includes
its built-in or custom program reference, standard parameters, texture references,
alpha/face modes and
other Panda render state. Meshes and clouds reference these IDs, so save/load retains
sharing and edits to the Studio material or an explicitly registered clone. Load
creates each material once before attaching it to objects; duplicate IDs, broken
references and invalid parameters reject the document. A registered clone receives
a new ID, while editing a shared material changes the one scene record used by its
objects.

An application-owned material remains a logical asset reference rather than a
scene-owned record. Its edits are persisted by the application; moving it into
scene-owned storage is an explicit operation, never an implicit side effect of a
Tools edit. Tools should show this ownership so a user can tell which save path
applies. Panda does not serialize application uniform bytes, `ShaderResources`
snapshots, compute buffers or solver data. A custom material that requires such
resources needs a stable application resource ID and resolver to reconstruct them;
otherwise save reports `NotSerializable` rather than writing an incomplete scene.

Application-owned material, program, texture and shader-resource references use
logical IDs resolved by an application `AssetResolver` at load time. A standard
built-in uses a reserved `panda:` ID; application shaders use the embedded
namespace/path key from the rendering contract.
Imported assets are referenced by their normalized path relative to an explicit
asset root plus a stable source node/mesh index, not an absolute machine path.
Runtime-created meshes/textures/programs need a caller-provided stable ID/resolver;
otherwise `Scene::save` returns `NotSerializable` with the offending ObjectId.
There is no hidden cooked database or binary scene snapshot.

A cloud persists its logical object ID, transform, bounds, material, shadow flags
and standard stream schema only. It saves no GPU buffer handles, count or particle
bytes. Loading creates a non-drawing cloud placeholder with count zero. The
application reconstructs its solver resources and calls `set_billboards` for that
ObjectId before simulation/rendering needs particles. A missing producer is visible
as an unresolved cloud diagnostic; it does not trigger a GPU readback or fabricate
particles. A separately saved domain cube is an ordinary mesh entity.

Design API: `Scene::save(path, SaveOptions) -> Result<void>`;
`Scene::load(Context&, path, AssetResolver&, LoadOptions) -> Result<Scene>`.
Load validates the complete document, supported exact version, references,
hierarchy, transforms, renderer settings and resource resolution before publishing
the new Scene. Failure leaves the old Scene untouched; load does not merge documents.
Reject duplicate IDs, cycles, non-finite/out-of-range fields, unknown Panda-owned
fields and unsupported versions with JSON path/object context. A future migration
requires a separately reviewed versioned adapter, not permissive guessing.

Save writes a unique temporary sibling file, flushes/closes, then replaces the
destination on the same filesystem. Existing files require `SaveOptions.overwrite`;
the default is false. Failure leaves the previous destination intact
where the platform permits replacement; report cleanup/replace failures and do not
claim crash-proof durability. Confirm exact target and never overwrite an unrelated
file merely because a path has the same stem. Scene IDs persist through ordinary
save/load. Deserializing into a new Scene changes runtime scene identity, invalidating
old Entity handles. Document merge/cross-file ObjectId lookup is not first-version API.

Application configuration is an independent file and schema owned by the application.
It stores simulation parameters, solver choice/seed/time step and references to
`documentId` plus ObjectIds for its cloud and domain. Panda provides ID conversion
and lookup, not a generic metadata blob or application serializer. The application
loads Scene first, checks the config's documentId, validates its own parameters,
then creates/publishes solver buffers. Scene and config are not atomically saved as a
pair; a mismatch is an explicit application error. A future project-container format
is outside version one.

### Import adapter

`Panda::Import` offers blocking `import_asset(Context&, AssetPath,
ImportOptions) -> Result<ImportedAsset>` and
`instantiate(Scene&, ImportedAsset const&, optional<Entity>) -> Result<vector<Entity>>`.
An ImportedAsset owns mesh/material/texture handles, the source node tree and
structured warnings; instantiate validates parent/context and commits a whole tree
or leaves Scene unchanged. Asset paths resolve beneath an explicit asset root;
absolute paths and traversal outside it are rejected. Relative texture paths use
the model file's directory, still bounded by that root. Cache images by canonical
path plus color/data usage and sampler policy. CPU mesh retention defaults off;
`retainCpuMesh=true` is explicit and reports its memory cost.

The initial tested subset is static glTF/GLB 2.0 with nodes, transforms, indexed
triangle meshes, UV0, vertex colors and core metallic/roughness material;
OBJ+MTL with basic diffuse/color/normal conversion; FBX static meshes/materials;
STL/PLY static geometry with available color. Triangulate polygons and generate
missing normals. Generate tangents only for usable UV0; warn and disable a normal
map otherwise. glTF alpha, double-sided and texture sampler modes map to Panda
material state; packed metallic B/roughness G/occlusion R follow the renderer
contract. Unsupported animations, skins, morphs, cameras, importer-specific lights,
extra UV sets, exotic material extensions and embedded behavior produce structured
warnings. `strict=true` turns lossy/unsupported warnings into an import error.
Do not advertise an entire Assimp extension list as tested support. Actual import
tests must include representative files and round-trip material/culling captures.

### Optional tools and safe edits

`Panda::Tools` owns an optional Dear ImGui/ImGuizmo overlay and explicit panels for
selection, transform, camera, light and material editing. A narrow callback registers
an application panel and receives an ImGui frame plus an intent sink; it can display
application parameters without reflection or automatic serialization. It does not
own the application's config object. The host chooses its input/camera controls;
Tools reports keyboard/mouse capture intent so a gizmo drag does not move the camera.

The frame order is poll input, draw UI and collect intents, validate/apply Scene
changes in the main-thread update phase, run application simulation/reset work,
then render. No Scene structural mutation or GPU buffer replacement occurs inside
an ImGui draw callback. A scene gizmo edits a copied Transform, requests local or
world mode explicitly and calls the checked Scene setter at the update boundary;
shear/invalid-parent errors keep the prior pose and appear in the panel. Undo/redo
is deferred. A simulation parameter edit has distinct `Apply` and `Reset` actions:
the application validates/builds replacement buffers and solver state, publishes
them at the resource update boundary, and retains old state on prepublication failure.
Panda does not infer which solver settings require a reset. Static/kinematic/physics
edits follow the [physics authority rules](physics.md#scene-integration-and-stepping).
Tools can be absent from a consumer build entirely.

Evidence after implementation: JSON round-trip with stable IDs and shared/edited
scene-owned materials; rejection of duplicate/broken material IDs and missing
application resource resolvers; unknown-version, duplicate-ID, bad-path,
missing-resource and failed-write cases; unresolved cloud
reconstruction; two-file document-ID mismatch; static glTF/OBJ/FBX/STL/PLY fixtures
with warnings and material captures; gizmo input-capture, failed keep-world edit and
simulation reset rollback.

References: [glTF 2.0 specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html),
[Assimp](https://github.com/assimp/assimp).
