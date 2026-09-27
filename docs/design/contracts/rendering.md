# Rendering and shader contract

Accepted specification for rendering, materials, shader interfaces and GPU billboards.
API sketches and layouts define intended behavior; implementation follows [the plan](../../development/PLAN.md).

## Rendering path

Use Vulkan 1.3 with dynamic rendering, synchronization2, and timeline semaphores.
Query and enable required features; a version number is not a substitute for checking
feature bits, formats, usages, and limits. Reject unsupported mandatory capabilities
with a useful diagnostic. Optional quality features may use a documented fallback.

Start with forward rendering. A few to a few dozen lights do not justify requiring
a deferred renderer, clustered lighting, or a general-purpose frame graph. Lights
are stored in appropriately sized GPU buffers, not a small fixed shader array.
Any practical budget or device limit is explicit and validated.

The intended path uses HDR intermediate lighting, tone mapping, and SDR output.
Depth follows the accepted [camera contract](scene.md#transforms-and-camera-conventions).
The exact formats, tone-mapping operator and presentation/color-space handling are
specified in the first-version renderer contract below. Full environment lighting/IBL is not an implicit
first-version requirement.

## Built-in materials

Provide a metallic-roughness lit material for direct lighting and an unlit material.
Simple ambient illumination is sufficient for the initial default scene.

Base color is the product of texture color, material tint, and instance tint. A
missing base-color texture or instance tint contributes white. This keeps textured
and color-only objects on the same predictable path.

Plan normal, metallic-roughness, occlusion, and emissive maps. Color and emissive
textures use sRGB decoding where appropriate; numeric data maps remain linear.
The channel convention, tangent requirements, and missing-tangent behavior are
specified in the material/import sections below.

Shader program, material parameters, and pipeline/render state are separate
concepts. Editing a color is not a reason to compile a pipeline. Sharing a material
shares its parameters; cloning is explicit.

## Alpha and face selection

| Mode | First-version behavior |
| --- | --- |
| Opaque | Depth test/write and the standard opaque rendering path |
| Mask | Explicit cutoff, depth test/write, matching clipping in shadow passes |
| Blend | Render after opaque objects, object-level back-to-front order, depth test on and depth writes off |

Blend does not promise correct arbitrary intersecting transparency, per-triangle
sorting, order-independent transparency, refraction, or colored transparent shadows.
GPU instances are not automatically read back to the CPU to sort them. Applications
may supply a GPU sorting/custom rendering path when needed.

`RenderFace::Front`, `Back`, and `Both` are ordinary material render state, not a
separate hierarchy of material classes. Face culling and shading-normal orientation
are distinct: the built-in shader must handle the selected back-facing surfaces
consistently, including normal mapping. The tangent/sign convention is below.

An inward-visible domain uses an ordinary cube with back-face rendering. Negative
scale is not the substitute. By default the domain may receive particle shadows but
does not cast shadows, so a visualization helper does not darken its entire interior.

## Lights and shadows

Support directional, spot, and point lights. Shadow casting is an independent
per-light choice with explicit resolution and coverage settings. The default scene
uses one shadow-casting directional light; enabling shadows for every light is not
assumed to be free.

Use one directional view and six cube-oriented point-light views in the shared
2D atlas. Shadow resolution, filtering and bias follow the rules below; no cascades
are required.

Opaque and masked geometry cast shadows, with matching mask behavior. Blended
objects do not cast by default. Cull shadow casters against the light volume, not
only the camera frustum. Instancing remains instancing in shadow passes.

Custom vertex deformation, clipping, or procedural shapes require an equivalent
shadow variant when shadows are desired. Panda cannot infer one from an arbitrary
shader. Default/custom shadow variant binding and validation are specified below.

## Anti-aliasing and sampling

- Support MSAA disabled, 2x, and 4x; request 4x by default and report the effective
  supported mode. Check the actual color/depth formats and attachment usages.
- Generate/use mip chains where appropriate and support trilinear sampling.
- Request 8x anisotropy by default, capped by the device limit; expose off/2x/4x/8x/16x.
  Unsupported anisotropy does not make an otherwise capable device unusable.
- Numeric/data textures and pixel-art use cases can override filtering and mip policy.
- MSAA addresses geometry-edge coverage, not all texture, specular, or temporal aliasing.
  Procedurally masked billboard edges need a separate quality check.
- TAA, motion-vector history, and advanced temporal reconstruction are not required.

## Instancing and billboards

Submit batches, not a draw call per instance. GPU particle data can be represented
by positions, radii, and colors and rendered with procedurally generated quads.
Do not require CPU entity updates or CPU copies proportional to particle count.

The accepted visual baseline is lit opaque sphere impostors, with the sphere surface
used for camera and shadow depth. Silhouette limitations, the data layout and shadow
variant are defined below, including near-plane projection edge cases.

GPU-only renderables provide conservative bounds, such as the simulation domain.
Changing count, buffers, or bounds follows the resource publication rules.

## Custom shaders and compilation

Applications supply complete GLSL shaders. Optional shared GLSL includes can expose
lighting helpers; there is no required surface-function hook, shader DSL, or graph.
Using an arbitrary shader does not automatically provide Panda's lighting or shadows.

Compile GLSL to SPIR-V during the build through a Panda CMake helper. Driver-specific
pipeline creation still happens for the actual device at runtime; warmup and a
validated pipeline cache reduce disruption. A shipped application does not need a
runtime GLSL compiler unless it explicitly enables an appropriate development feature.

Optional development hot reload compiles and validates a replacement before
publication. Failure keeps the last valid program. Changed descriptor/block layouts
must not reinterpret old material or buffer memory silently.

Panda uses SPIRV-Reflect to check descriptors, push constants, and block layouts
when programs load/reload. Reflection is not SPIR-V validation; use `spirv-val` in
the shader build/reload tooling. Neither infers read/write hazards, CPU padding
correctness, or an algorithm's meaning. Verify host/shader layout with explicit tests.

The accepted standard draw interface reserves set 0 for scene data, set 1 for
materials, set 2 for draw/instance data, and set 3 for application resources.
Concrete binding numbers, types, alignment, capacity, push constants, vertex inputs,
and compatibility rules are specified in the first-version renderer contract below.
Independent Vulkan compute programs are not forced into a material descriptor layout.

## Shader integration and billboard scope

### Programs, materials, and application resources

Keep three concepts, not a class hierarchy:

- `ShaderProgram`: an owning, context-bound, shared program identity whose current
  revision is immutable (vertex/fragment SPIR-V and an explicit shadow selection).
  Reload replaces that identity's current revision; submitted frames retain the old one.
- `Material`: a shared editable description using a built-in or custom program,
  standard parameters/textures and render state. Clone for private changes.
- `ShaderResources`: an immutable owning binding snapshot for set 3 of a particular
  program interface. Replace it during the update phase; in-flight draws retain the
  previous snapshot. No global string-to-variant material property system is needed.

Use `Context::create_program(ProgramDesc) -> Result<ShaderProgram>` and
`Context::create_material(MaterialDesc) -> Result<Material>`. Ordinary materials
select BuiltinLit or BuiltinUnlit and need neither factory for custom programs nor
any application resource bindings. Custom programs remain full GLSL; they may reuse
the built-in vertex shader and optional lighting includes. They are not functions
inserted into generated shader source.

Sets 0/1/2 remain Panda-owned scene/material/draw data; applications must not repurpose
them. Set 3 initially supports individual uniform-buffer, read-only storage-buffer,
and combined sampled-texture bindings in vertex/fragment stages. Bindings are numeric
and checked against reflection, including image dimension/sample type. Defer descriptor
arrays, bindless resources, storage-image writes and fragment-buffer writes from this
convenience path. Native compute keeps its independent Vulkan layout and operations.
Reject unsupported SPIR-V capabilities/descriptors instead of enabling features silently.

A resource snapshot copies uniform bytes at creation and holds owning Buffer/Texture
handles for GPU resources. Buffer slices have checked offset/range/usage/alignment;
all resources must belong to the program's context. The renderer includes these
declared reads in the [compute protocol](compute.md), including custom shadow reads.
Shader writes are not permitted in this draw path. Read-only binding does not prevent a preceding
native compute stage from writing the buffer under the accepted compute protocol.

An application defines its own C++ parameter struct and matching GLSL block, uploads
explicit bytes, and tests offsets/size. No automatic C++ reflection or parameter GUI
is implied. Use std140 for uniform blocks and std430 for storage blocks. Panda's
standard blocks use explicit 16-byte groups, not raw public GLM/Transform storage;
the full ABI table below provides the standard offsets and strides.

One snapshot can be shared by many materials/draws. An explicit per-renderable override
allows different application buffers without cloning a material for every object.
Different resource snapshots form separate batches; instance data is the route to
per-instance variation in one batch. Uniform edits publish data, not new shader code.

`ProgramDesc` explicitly chooses a standard-mesh or procedural vertex input contract.
Camera passes require vertex and fragment entry points named `main`. Shadow selection
is one of None, StandardMesh, or Explicit(vertex, optional fragment). StandardMesh is
an application assertion that undeformed mesh positions and the standard material
mask are correct; it is never inferred from arbitrary shader code. Explicit variants
share the program's resource interface, and provide their own deformation/discard.
Requesting shadow casting with None is a preflight error, not an invisible fallback.
Built-ins supply their own matching variants. This is two pass variants, not a new
general pass/plugin framework.

### Build, loading, and reload boundary

Extend the accepted `panda_add_shaders` helper to run `glslc` for Vulkan 1.3 and
`spirv-val` with the matching target environment. Both are build/development tools,
not runtime release dependencies. Produce `<panda_shaders/NAME.hpp>` for a validated
single-identifier NAMESPACE `NAME`, with `NAME::shaders::get(relativeSourcePath)`
returning `Result<ShaderCode>`. Paths are normalized source-relative keys including
extensions, not absolute machine paths or ambiguous basenames. Reject duplicate
namespaces/keys within a consuming target. The generated namespace contains no global
registration with Panda and no runtime filesystem search.

`ShaderCode` is a view of aligned SPIR-V words with a stage and diagnostic label;
embedded storage lasts for the process. The program factory takes/copies what it
needs before returning, so callers may also supply temporary application-loaded
SPIR-V. Runtime release loading checks the binary header, reflected interface and
enabled capabilities but is not a sandbox for malicious shader binaries. The
supported production route is validated build output, not arbitrary downloaded code.

SPIR-V creation at build time does not remove device-specific pipeline compilation.
Offer explicit warmup for the scene's known program/state combinations, and report
pipelines first created after warmup. Development first-use creation may still occur;
benchmarks exclude warmup and flag steady-state pipeline creation. Cache persistence
and exact pipeline keys are specified in the first-version renderer contract below.

Hot reload is explicit tooling: compile, validate, reflect, prepare replacement
pipelines, then publish the whole camera/shadow revision at an update boundary.
Failure keeps the old revision; old GPU uses retain it until completion. A changed
set/block/vertex/push-constant interface is rejected for live replacement and requires
explicit program/material/resource recreation. Resource meaning cannot be inferred
from equal byte sizes; the application remains responsible for semantic compatibility.
No implicit migration of old parameter bytes is allowed.

### GPU cloud representation

Provide `Scene::add_billboards(BillboardCloudDesc) -> Result<Entity>` and
`Scene::set_billboards(Entity, BillboardCloudDesc) -> Result<void>`. The latter
validates and atomically replaces the binding description for future frames; it does
not reinitialize the application solver. A cloud is one entity and one instanced
batch per applicable pass, split only if a documented device/range limit requires it.

The description contains a required position/radius Buffer slice, optional color
slice, uint32 instance count, Material, optional ShaderResources override, conservative
local AABB, and cast/receive-shadow flags. Empty count is legal and emits no draw.
Capacity follows from slice sizes and stride; reject a count exceeding either supplied
slice. No CPU count-sized scan or particle upload is part of scene preparation.

Standard data layout, std430 arrays with no header:

| Buffer | GLSL element | Stride | Meaning |
| --- | --- | --- | --- |
| Position/radius | `vec4` | 16 bytes | xyz center in cloud-local meters; w radius |
| Optional color | `vec4` | 16 bytes | linear RGBA tint; missing stream contributes white |

Offsets obey Vulkan storage-buffer alignment and ranges cover the declared count.
Use a small C++20-compatible plain float-field record with asserted offsets/size for
CUDA producers; do not require engine/GLM headers or assume CUDA `float3` packing.
A producer writes directly to the shared allocation or uses its own GPU conversion
kernel when its private layout differs. Panda does not translate solver layouts or
read particles back. Custom GLSL may instead consume application layouts through set 3.

Finite centers/colors and nonnegative finite radii are producer invariants, not CPU
validation promises. Radius zero disables a slot; negative/NaN data is invalid. The
initial draw count is CPU-known; GPU compaction/indirect-count convenience is deferred,
not silently implemented with a readback. Changing CPU-known count is O(1).

Cloud transforms allow translation, rotation and positive uniform world scale only;
scale multiplies radii. Reject reflection, nonuniform scale or shear for the built-in
sphere path rather than turning spheres into undocumented ellipsoids. World-space
simulation data uses an identity cloud transform. The physical domain remains an
independent entity, not the cloud's parent by default.

### Sphere billboards, not a fluid surface

The retained particle vertex shader generates six vertices per instance. Its fragment
shader already performs ray/sphere intersection for lighting, but never writes the
sphere's depth: rasterized quad depth remains. This is source inspection, not a
measured runtime defect. See the archived
[vertex](../../../legacy/drip-engine/src/gfx/shader/vs/Particles.vert) and
[fragment](../../../legacy/drip-engine/src/gfx/shader/fs/Particles.frag) shaders.

Use a lit sphere-impostor baseline: conservative procedural quad, ray/sphere hit,
surface normal and hit position for lighting/shadow lookup, and projected hit depth
for the depth buffer. Discard ray misses. Camera and shadow passes must use the same
sphere geometry, including all six point-light views. Do not give particles spherical
lighting but flat-quad occlusion. Opaque color is the default; this is not transparent
water, surface reconstruction, refraction or a metaball renderer.

Conservative screen bounds must cover off-axis perspective spheres, orthographic
views and near-plane intersections; the old center-plane radius quad is not a proof
of coverage. Do not force early fragment tests that bypass the required shader depth.
The first-version renderer contract below specifies near/inside-sphere behavior and
the conservative projection algorithm. Writing shader depth
may cost early-depth efficiency; profile it instead of claiming free correctness.

Baseline silhouettes use ray-hit discard at the normal fragment shading frequency.
MSAA does not promise perfectly sample-shaded analytic sphere edges. Per-sample
intersection and transparent sorting are not implicit requirements. Include enlarged
silhouette/intersection captures in acceptance and report this quality limitation.

Required evidence for this slice: a custom GLSL material without an engine edit;
application UBO and compute-written SSBO reads; wrong-layout/foreign-context rejection;
transactional camera+shadow reload; missing-shadow-variant error; constant CPU object
and draw counts as particle count increases; sphere depth against a mesh reference;
world-scale/domain-independence tests.

## First-version renderer contract

Names are design signatures; byte layouts and behavior are contractual, subject
to implementation probes.

### Draw interface and explicit host/shader layout

The ordinary `Context::render(Scene&)` path prepares draw data and calls the same
renderer used by `Context::render(RenderView)`. `RenderView` is a transient value
containing one camera, lights, and mesh/cloud draw descriptions with owning resource
handles and copied transforms. The renderer copies/retains everything needed for
submission before returning. It owns no ECS entity and does not serialize a view.
It allows explicit rendering without a Scene while keeping only one Vulkan path.
Both overloads use the [compute](compute.md#submission-protocol) and
[frame](resources.md#frame-and-window-state) protocols and require a valid camera.

All standard blocks use 32-bit IEEE floats/uints, column-major matrices, and
`std140` UBO or `std430` SSBO layouts. C++ packing helpers use explicit float/uint
arrays and `offsetof` assertions; public GLM/Transform memory is not copied wholesale.
No push constants, scalar-block-layout feature, runtime descriptor arrays, or
update-after-bind are used by the standard graphics pipeline. Buffer descriptors
use checked device alignment; the dynamic draw UBO stride is rounded up to
`minUniformBufferOffsetAlignment` (at least its 160-byte payload).

| Set.binding | Descriptor | Contents / stages |
| --- | --- | --- |
| 0.0 | Uniform buffer, 320 bytes | `FrameBlock`, vertex+fragment |
| 0.1 | Read-only storage buffer | `LightRecord[]`, 80-byte stride, fragment |
| 0.2 | Read-only storage buffer | `ShadowView[]`, 80-byte stride, fragment |
| 0.3 | Combined depth-compare image sampler | Single 2D shadow atlas, fragment |
| 1.0 | Uniform buffer, 64 bytes | `MaterialBlock`, fragment |
| 1.1–1.5 | Five combined image samplers | Base color, normal, metallic/roughness, occlusion, emissive; fragment |
| 2.0 | Dynamic uniform buffer, 160-byte payload | `DrawBlock`, vertex+fragment |
| 2.1 | Read-only storage buffer | `MeshInstance[]`, 144-byte stride, vertex |
| 2.2 | Read-only storage buffer | Cloud `vec4 centerRadius[]`, vertex+fragment |
| 2.3 | Read-only storage buffer | Cloud `vec4 color[]`, vertex+fragment; white dummy if absent |
| 3.* | Reflected individual bindings | Optional application resources, vertex/fragment |

Unused built-in slots bind shared valid dummy resources; reflected custom programs
may omit unused bindings but may not change the type/count of a Panda-owned slot.
Set 3 is empty if unused. Standard set layouts are fixed for a Context; reflections
must match the used reserved bindings, vertex inputs and block member offsets/strides.
Reject unknown reserved bindings, writable standard buffers, incompatible stage
interfaces and a pipeline layout over device limits. Application uniform blocks
have explicit size/offset checks against reflected metadata before publication.

`FrameBlock` offsets: view 0, projection 64, viewProjection 128,
inverseViewProjection 192 (`mat4` each); cameraPosition/exposure 256,
ambientRgb/intensity 272, directional/point/spot/shadowView counts 288 (`uvec4`),
viewportWidth/height/inverseWidth/inverseHeight 304 (`vec4`). Shadow passes replace
view, projection, viewProjection, inverseViewProjection, and camera position with
values for the light face while retaining the same layout; no camera-pass matrix
remains in those fields.
Exposure defaults to 1.0. Ambient defaults to linear RGB (1,1,1) at intensity 0.06.

`LightRecord` is five `vec4`-sized groups at offsets 0/16/32/48/64:
`positionRange`, `directionInnerCos`, `colorIntensity`,
`outerCosDepthBiasNormalBiasPcfRadius`, and `uvec4 typeShadowFirstViewViewCount`.
Type values 0/1/2 mean directional/point/spot. Direction points from the light into
the scene. Color is linear; intensity is a nonnegative linear multiplier. Range is
finite positive for local lights. Cone cosines satisfy inner >= outer. No fixed
light array appears in GLSL: the SSBO is sized to the count, with an explicit
default `maxLights=64` Context budget configurable at creation up to checked device
buffer/descriptor limits. Exceeding the budget returns an error, never silently
drops lights. The `ShadowView` array holds column-major world-to-shadow-clip `mat4`
at offset 0 and normalized atlas rect `vec4(x,y,width,height)` at 64.

`MaterialBlock` is four `vec4` groups at offsets 0/16/32/48: linear baseColorTint;
linear emissiveRgb/intensity; metallic/roughness/occlusionStrength/normalScale;
alphaCutoff and three reserved zeros. Built-in default: tint white, emissive black,
metallic 0, roughness 1, occlusion strength 1, normal scale 1, cutoff 0.5.
Clamp validated metallic/roughness/strength to [0,1] and normal scale to [0,2];
reject non-finite inputs. A missing texture uses a bound semantic neutral texture.

`DrawBlock`: model `mat4` at 0, normal matrix stored as `mat4` at 64 (upper-left
inverse-transpose 3x3, remaining cells canonical identity/zero), linear tint `vec4`
at 128, and `uvec4(baseInstance, mode, 0, 0)` at 144. Mode 0 reads DrawBlock for a
single mesh; mode 1 indexes `MeshInstance[baseInstance+gl_InstanceIndex]`; mode 2
indexes cloud streams and uses DrawBlock model as the cloud transform. All draws use
firstInstance=0. `MeshInstance` is model `mat4` 0, normal `mat4` 64, tint `vec4` 128.
The standard mesh binding has 64-byte stride: location 0 position `R32G32B32_SFLOAT`
offset 0, 1 normal same offset 12, 2 UV `R32G32_SFLOAT` offset 24, 3 tangent
`R32G32B32A32_SFLOAT` offset 32, 4 linear vertex color
`R32G32B32A32_SFLOAT` offset 48 (white when absent in source). Procedural cloud
has no vertex binding and six
generated vertices per instance. Custom shaders choose one of these input contracts.
The shader should not assume a CPU-visible particle object.

### Materials, faces, and output

Built-in lit uses direct-light GGX/Smith/Schlick metallic/roughness BRDF with
dielectric F0=0.04, Lambert diffuse, a simple ambient term and the material maps;
unlit uses base color/emissive. Base color multiplies texture, material tint,
instance tint and vertex color (white when absent). Base color texture and emissive
texture decode sRGB to linear. Normal, metallic/roughness and occlusion decode linear.
Metallic/roughness texture uses B/G channels, occlusion R, tangent-space normal XYZ.
Missing normal map uses (0.5,0.5,1), missing metallic/roughness uses (1,1,1),
missing occlusion uses white, missing emissive black. Import computes missing normals
and tangents where UVs allow; if UV/tangents are unusable it disables the normal map
with a warning. Tangent `.w` is handedness and bitangent is
`cross(N,T)*tangent.w*sign(det(model))` after normal-matrix transform and tangent
orthogonalization. For a rendered back face, flip N before forming the bitangent.
The projection applies the Vulkan Y flip once; positive viewport height and
clockwise front face represent positive-determinant meshes. A negative world
determinant reverses front-face state in camera and shadow passes. Face mode controls
culling independently of the corrected shading normal.

Use `R16G16B16A16_SFLOAT` HDR color and MSAA resolve attachments, checking required
format/sample/usage support. Prefer `D32_SFLOAT` camera/shadow depth, allow
`D16_UNORM` if sampled-depth/attachment capabilities support it, and report the
effective format. If no required HDR/depth format is usable, fail Context creation
with a diagnostic. Tone-map by the fixed ACES fitted curve
`clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14),0,1)` per channel with exposure 1 then
encode SDR sRGB. Prefer an SRGB swapchain format/colorspace, otherwise UNORM plus
explicit sRGB encoding; reject a surface lacking either supported SDR route.
UI overlays are composited in SDR after tone mapping. Clear HDR to (0.025,0.035,
0.05,1). Report effective color/depth formats, sample count and anisotropy.

### Shadows, quality, and pipelines

Use one sampled 2D depth atlas per frame slot. A shadowed directional/spot light
uses one tile; point lights use six cube-oriented views in six tiles and reconstruct
the selected face in shader. This implements the accepted six-view point-shadow
behavior without requiring descriptor-array indexing or cube-array support. The
default atlas is 4096 square, packed deterministically in 256-pixel cells with a
four-texel guard per tile; requested face resolutions are 256/512/1024/2048.
Defaults: Studio directional 2048 with 30 m orthographic width and near/far 0.1/80 m;
direction normalized (-0.4,-1,-0.3), linear white color and intensity 3;
spot 1024; point faces 512. Invalid requests, unsupported image dimensions or
insufficient atlas area return a clear error before publishing settings. No silent
resolution reduction or shadow-light dropping. A one-pixel sampler footprint stays
inside a guarded tile; clamps prevent PCF leakage across neighbors. Use 3x3 PCF;
initial depth bias 0.0005 and normal offset 0.002 m are configurable starting values
that require visual tuning, not verified quality claims. Atlas rebuild/resize follows
[resource retirement rules](resources.md#frame-contexts-and-retirement). A depth-only
masked variant samples the base-color alpha with the same cutoff as the camera pass. Blend casts no shadow by default.

Select MSAA 4x, then 2x, then off based on both actual HDR/depth format capabilities;
report the effective mode. Use trilinear mips and request 8x AF up to the device
limit; if anisotropy is unavailable use trilinear without it. Record requested and
effective values. Shadow and color format fallback is capability driven, not a
performance auto-tuner. The sphere edge remains a documented masked silhouette.

A pipeline key includes shader revision/hash, vertex contract, pass kind, descriptor
layout signature, color/depth formats, sample count, alpha mode, culling/front-face,
and depth state. Material tints/roughness do not enter the key. Use a Vulkan pipeline
cache with a versioned application cache directory. Before loading, check the Vulkan
cache header size/version, vendor/device IDs and pipelineCacheUUID against the
selected physical device; an invalid or corrupt cache is ignored. Save only on an
administrative path via atomic file replacement. Warmup covers known scene program
and state combinations; first use outside warmup is reported. Cache correctness
never substitutes for the actual key or shader-interface validation.

### Sphere projection edge cases and tests

In the GPU vertex stage, project the eight corners and near-plane-clipped edges of
each sphere's conservative view-space AABB, clamp the resulting screen rect to the
viewport, then draw its quad. No CPU particle traversal is involved.
If the view origin lies inside the sphere, use the full viewport rect. The fragment
ray tests both sphere roots and writes the nearest positive hit within the pass's
near/far clip range; misses discard. This applies to orthographic/perspective camera
and directional/spot/point shadow views. A radius-zero slot discards. Verify the
bound against a tessellated sphere across off-axis, near-clipped and inside cases;
do not rely on the old center-plane square. The GPU can reduce conservative bounds
later without changing the public cloud representation.

Acceptance requires reflected offset/stride tests, CPU projection/decomposition
tests, a GPU first cube with shadow, each light type, 65 lights with an enlarged
budget, camera/shadow alpha agreement, back-face/negative-scale/tangent checks,
tone-map and color-space captures, format/MSAA/AF fallback, wrong-cache rejection,
custom resource/shadow reload rejection, and sphere-versus-mesh depth captures.
Use RenderDoc traces and timestamps to verify draws and transitions.

## Required evidence

Test color-only/textured/instanced tint equivalence, color-space handling, negative
scale, both-sided normals, alpha clipping in camera and shadow passes, and light
counts above legacy fixed limits. Capture directional/spot/point shadows, AA mode
fallback, mip transitions, and custom-shader layout rejection. Measure actual draw
and submission counts for a GPU cloud rather than judging performance visually.

References: [Vulkan versions](https://docs.vulkan.org/guide/latest/versions.html),
[SPIRV-Reflect](https://github.com/KhronosGroup/SPIRV-Reflect),
[shader memory layout](https://docs.vulkan.org/guide/latest/shader_memory_layout.html),
[fragment/depth operations](https://docs.vulkan.org/spec/latest/chapters/fragops.html),
[pipeline caches](https://docs.vulkan.org/guide/latest/pipeline_cache.html),
[sampler configuration](https://docs.vulkan.org/refpages/latest/refpages/source/VkSamplerCreateInfo.html),
[glTF material conventions](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html).
