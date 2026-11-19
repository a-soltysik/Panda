# Rendering and shader contract

Accepted specification for rendering, materials, shader interfaces and procedural draws. The behavior and layouts define
requirements; implementation follows [the plan](../../development/PLAN.md).

## Rendering path

Use Vulkan 1.3 with dynamic rendering, synchronization2, and timeline semaphores. Query and enable required features; a
version number is not a substitute for checking feature bits, formats, usages, and limits. Reject unsupported mandatory
capabilities with a useful diagnostic. Optional quality features may use a documented fallback.

Start with forward rendering. A few to a few dozen lights do not justify requiring a deferred renderer, clustered
lighting, or a general-purpose frame graph. Lights are stored in appropriately sized GPU buffers, not a small fixed
shader array. Any practical budget or device limit is explicit and validated.

The intended path uses HDR intermediate lighting, tone mapping, and SDR output. Depth follows the
accepted [camera contract](scene.md#transforms-and-camera-conventions). The exact formats, tone-mapping operator and
presentation/color-space handling are specified in the first-version renderer contract below.
For v1, built-in Lit materials support opt-in [image-based lighting](../../development/tasks/environment-lighting.md)
from an application-supplied environment. It replaces the simple ambient term with diffuse environment lighting
and adds roughness-dependent reflections. Direct lights apply with or without an environment.

Windowed Engines request FIFO, MAILBOX or IMMEDIATE presentation through
`EngineOptions::presentationMode`; FIFO is the default. Panda uses the requested
mode exactly and returns `Unsupported` when the surface does not support it.
`WindowRenderer::getEffectivePresentationMode()` reports the active swapchain
mode when a generation exists. Swapchain recreation checks support
again before creating each generation. FIFO waits for vertical blanking,
MAILBOX replaces a pending frame with the newest request at vertical blanking,
and IMMEDIATE may tear because it does not wait. See the
[Vulkan present-mode reference](https://docs.vulkan.org/refpages/latest/refpages/source/VkPresentModeKHR.html)
for the normative behavior.

## Offscreen output

Applications can render a `RenderView` into an explicit single-sample SDR offscreen color target with depth, without a
window surface or swapchain. It uses the built-in renderer's normal material, lighting, alpha, shadow, tone-mapping and
output-encoding behavior. GPU rendering stays asynchronous and returns a completion point; copying pixels to host memory
is explicit and follows that completion rather than making ordinary rendering wait. The offscreen output has a stable
extent, channel order and color-space contract for image consumers and pixel tests. Image-file encoding and a general
render graph are outside this capability.

`ResourceManager::createOffscreenTarget()` creates an RGBA8 sRGB target. Applications pass a target to each
`OffscreenRenderer::render()` call and may read the latest completed output as tightly packed, top-to-bottom RGBA bytes;
RGB is sRGB encoded and alpha is linear UNORM. `OffscreenTargetDesc::extent` and
`ImageReadback::extent` are `glm::uvec2` pixel values with x as width and y as
height. `WindowRenderer::renderWithCapture()` renders once into that target,
copies the same output to the swapchain image and presents it.

## Built-in materials

Provide a metallic-roughness lit material for direct lighting and an unlit material. Simple ambient illumination is
sufficient for the initial default scene.

Base color is the product of texture color, material tint, instance tint, and optional vertex color. A missing
base-color texture, instance tint, or vertex color contributes white. If supplied, the first built-in base-color map
must be an uploaded, sampled `R8G8B8A8Srgb` or `B8G8R8A8Srgb` texture from the same Engine, and its mesh must declare
`TexCoord0`.
Built-in material maps use the Engine-wide `TextureFilteringPreset`: Nearest,
Bilinear, Trilinear, or Anisotropic2x/4x/8x/16x. Anisotropic presets use
trilinear filtering and are capped at the device limit; unsupported anisotropy
falls back to trilinear. The default is Anisotropic8x. `TextureDesc::mipPolicy`
chooses single-level, supplied, or CPU-generated mip data, while
`TextureDesc::addressMode` applies repeat, mirrored repeat, or clamp-to-edge to
both UV axes. Filtering is not configured per texture in this API. sRGB texture
formats decode color channels to linear values while alpha remains linear. The
sampled alpha participates in Mask and Blend with material, draw, and vertex
alpha.
Applications choose the initial preset with `EngineOptions::textureFilteringPreset`
and can change it later with `Engine::setTextureFilteringPreset()`; changes apply
to later window and offscreen frames. `Engine::getTextureFilteringPreset()` reports
the current request.

The optional Lit metallic-roughness map must be an uploaded, sampled `R8G8B8A8Unorm` or `B8G8R8A8Unorm` texture from
the same Engine, and its mesh must declare `TexCoord0`. Its blue channel multiplies the material metallic value and its
green channel multiplies roughness; a missing map contributes white. Numeric channels remain linear.

The optional Lit tangent-space normal map must be an uploaded, sampled `R8G8B8A8Unorm` or `B8G8R8A8Unorm` texture from
the same Engine. Its mesh needs `TexCoord0` and `Tangent` for normal mapping; if either is absent, Panda ignores the
map
and logs a warning while rendering with the interpolated mesh normal. `normalScale` defaults to 1 and multiplies the
normal's tangent-space X and Y components without a fixed range; it must be finite. Normal maps do not change geometry
or the shadow silhouette.

The optional Lit occlusion map must be an uploaded, sampled `R8G8B8A8Unorm` or `B8G8R8A8Unorm` texture from the same
Engine, and its mesh must declare `TexCoord0`. Its red channel stores ambient occlusion: white leaves ambient
illumination unchanged, while darker values reduce it. `occlusionStrength` defaults to 1, must be finite, and is
clamped to [0,1] when uploaded. The shader multiplies the ambient term by
`mix(1, occlusionTexture.r, occlusionStrength)`; direct lighting and dynamic shadows are unchanged. A missing map is
equivalent to white.

Shader program, material parameters, and pipeline/render state are separate concepts. Editing a color is not a reason to
compile a pipeline. Sharing a material shares its parameters; cloning is explicit.

## Alpha and face selection

| Mode   | First-version behavior                                                                                       |
|--------|--------------------------------------------------------------------------------------------------------------|
| Opaque | Depth test/write and the standard opaque rendering path                                                      |
| Mask   | Explicit cutoff, depth test/write, matching clipping in shadow passes                                        |
| Blend  | Straight-alpha source-over after Opaque/Mask, object-level back-to-front order, depth test on and writes off |

Blend uses the product of material, draw and optional vertex alpha, clamped to [0,1]. Blended meshes do not cast
shadows. Blend does not promise correct arbitrary intersecting transparency, per-triangle sorting,
order-independent transparency, refraction, or colored transparent shadows. GPU instances are not automatically read
back to the CPU to sort them. Applications may supply a GPU sorting/custom rendering path when needed.

`RenderFace::Front`, `Back`, and `Both` are ordinary material render state, not a separate hierarchy of material
classes. Face culling and shading-normal orientation are distinct: the built-in shader must handle the selected
back-facing surfaces consistently, including normal mapping. The tangent/sign convention is below.

An inward-visible domain uses an ordinary cube with back-face rendering. Negative scale is not the substitute. By
default the domain may receive particle shadows but does not cast shadows, so a visualization helper does not darken its
entire interior.

## Lights and shadows

Support directional, spot, and point lights. Shadow casting is an independent per-light choice with explicit resolution
and coverage settings. The default scene uses one shadow-casting directional light; enabling shadows for every light is
not assumed to be free.

Use one directional or spot view and six cube-oriented point-light views in the shared 2D atlas. Shadow resolution,
filtering and bias follow the rules below; no cascades are required.

Opaque and masked geometry cast shadows, with matching mask behavior. Blended objects do not cast by default. Cull
shadow casters against the light volume, not only the camera frustum. Instancing remains instancing in shadow passes.

Custom vertex deformation, clipping, or procedural shapes require an equivalent shadow variant when shadows are desired.
Panda cannot infer one from an arbitrary shader. Default/custom shadow variant binding and validation are specified
below.

## Anti-aliasing and sampling

- Mesh rendering takes `RenderView::requestedSampleCount` for each frame, with 4x by default.
  Choose the highest supported count at or below the request from 1x, 2x and 4x, using
  the actual color/depth formats and attachment usages. The window and offscreen renderers
  each expose a support query. The offscreen query also applies to capture frames.
  `FrameResult::effectiveSampleCount` reports the submitted count. Clear-only frames use 1x.
- Generate/use mip chains where appropriate and support nearest, bilinear,
  trilinear, and anisotropic 2x/4x/8x/16x Engine-wide filtering presets.
- Request 8x anisotropy by default and cap it at the device limit. Unsupported
  anisotropy falls back to trilinear without making an otherwise capable device unusable.
- MSAA addresses geometry-edge coverage, not all texture, specular, or temporal aliasing. Procedurally masked billboard
  edges need a separate quality check.
- TAA, motion-vector history, and advanced temporal reconstruction are not required.

## Instancing and billboards

Submit batches, not a draw call per instance. GPU particle data can be represented by positions, radii, and colors and
rendered with procedurally generated quads. Do not require CPU entity updates or CPU copies proportional to particle
count.

The accepted visual baseline is lit opaque sphere impostors, with the sphere surface used for camera and shadow depth.
Silhouette limitations, the data layout and shadow variant are defined below, including near-plane projection edge
cases.

GPU-only renderables provide conservative bounds, such as the simulation domain. Changing count, buffers, or bounds
follows the resource publication rules.

## Custom shaders and compilation

Applications supply complete GLSL shaders. Optional shared GLSL includes can expose lighting helpers; there is no
required surface-function hook, shader DSL, or graph. Using an arbitrary shader does not automatically provide Panda's
lighting or shadows.

Compile GLSL to SPIR-V during the build through a Panda CMake helper. Driver-specific pipeline creation still happens
for the actual device at runtime; warmup and a validated pipeline cache reduce disruption. A shipped application does
not need a runtime GLSL compiler unless it explicitly enables an appropriate development feature.

Optional development hot reload compiles and validates a replacement before publication. Failure keeps the last valid
program. Changed descriptor/block layouts must not reinterpret old material or buffer memory silently.

Panda uses SPIRV-Reflect to check descriptors, push constants, and block layouts when programs load/reload. Reflection
is not SPIR-V validation; use `spirv-val`in the shader build/reload tooling. Neither infers read/write hazards, CPU
padding correctness, or an algorithm's meaning. Verify host/shader layout with explicit tests.

The standard draw interface separates renderer-owned data from application shader resources. Choose descriptor sets,
bindings and packing when implementing each interface; group resources by use and update frequency where useful.
Independent Vulkan compute programs are not forced into a material descriptor layout.

## Shader integration

### Programs, materials, and application resources

Keep three concepts, not a class hierarchy:

- `ShaderProgram`: an owning, context-bound, shared program identity whose
  current revision is immutable (vertex/fragment SPIR-V and an explicit shadow
  selection). Reload replaces that identity's current revision; submitted frames
  retain the old one.
- `Material`: a shared editable description using a built-in or custom program,
  standard parameters/textures and render state. Clone for private changes.
- `ShaderResources`: an immutable owning binding snapshot for application
  resources of a particular program interface. Replace it during the update
  phase; in-flight draws retain the previous snapshot. No global
  string-to-variant material property system is needed.

Provide creation of context-bound shader programs and materials. Ordinary
materials select BuiltinLit or BuiltinUnlit and need neither factory for custom
programs nor any application resource bindings. Custom programs remain full
GLSL; they may reuse the built-in vertex shader and optional lighting includes.
They are not functions inserted into generated shader source.

Keep Panda-owned frame, material and draw data separate from application-owned
shader resources. Application resources initially support individual
uniform-buffer, read-only storage-buffer, and combined sampled-texture bindings
in vertex/fragment stages. Bindings are numeric and checked against reflection,
including image dimension/sample type. Defer descriptor arrays, bindless
resources, storage-image writes and fragment-buffer writes from this convenience
path. Native compute keeps its independent Vulkan layout and operations. Reject
unsupported SPIR-V capabilities/descriptors instead of enabling features
silently.

A resource snapshot copies uniform bytes at creation and holds owning
Buffer/Texture handles for GPU resources. Buffer slices have checked
offset/range/usage/alignment; all resources must belong to the program's
context. The renderer includes these declared reads in the
[compute protocol](compute.md), including custom shadow reads. Shader writes are
not permitted in this draw path. Read-only binding does not prevent a preceding
native compute stage from writing the buffer under the accepted compute
protocol.

An application defines its own C++ parameter struct and matching GLSL block,
uploads explicit bytes, and tests offsets/size. No automatic C++ reflection or
parameter GUI is implied. The renderer validates concrete standard block layouts
against the implemented shader interface rather than assuming public
GLM/Transform storage.

One snapshot can be shared by many materials/draws. An explicit per-renderable
override allows different application buffers without cloning a material for
every object. Different resource snapshots form separate batches; instance data
is the route to per-instance variation in one batch. Uniform edits publish data,
not new shader code.

`ProgramDesc` explicitly chooses a standard-mesh or procedural vertex input
contract. Camera passes require vertex and fragment entry points named `main`.
Shadow selection is one of None, StandardMesh, or Explicit (vertex, optional
fragment). StandardMesh is an application assertion that undeformed mesh
positions and the standard material mask are correct; it is never inferred from
arbitrary shader code. Explicit variants share the program's resource interface,
and provide their own deformation/discard. Requesting shadow casting with None
is a preflight error, not an invisible fallback. Built-ins supply their own
matching variants. This is two pass variants, not a new general pass/plugin
framework.

### Build, loading, and reload boundary

Provide build tooling to run `glslc` for Vulkan 1.3 and `spirv-val` with the
matching target environment. Both are build/development tools, not runtime
release dependencies. Generate a namespaced C++ lookup for embedded shader code.
Paths are normalized source-relative keys including extensions, not absolute
machine paths or ambiguous basenames. Reject duplicate namespaces/keys within a
consuming target. The generated namespace contains no global registration with
Panda and no runtime filesystem search.

`ShaderCode` is a view of aligned SPIR-V words with a stage and diagnostic
label; embedded storage lasts for the process. The program factory takes/copies
what it needs before returning, so callers may also supply temporary
application-loaded SPIR-V. Runtime release loading checks the binary header,
reflected interface and enabled capabilities but is not a sandbox for malicious
shader binaries. The supported production route is validated build output, not
arbitrary downloaded code.

SPIR-V creation at build time does not remove device-specific pipeline
compilation. Offer explicit warmup for the scene's known program/state
combinations, and report pipelines first created after warmup. Development
first-use creation may still occur; benchmarks exclude warmup and flag
steady-state pipeline creation. Cache persistence and exact pipeline keys are
specified in the first-version renderer contract below.

Hot reload is explicit tooling: compile, validate, reflect, prepare replacement
pipelines, then publish the whole camera/shadow revision at an update boundary.
Failure keeps the old revision; old GPU uses retain it until completion. A
changed set/block/vertex/push-constant interface is rejected for live
replacement and requires explicit program/material/resource recreation. Resource
meaning cannot be inferred from equal byte sizes; the application remains
responsible for semantic compatibility. No implicit migration of old parameter
bytes is allowed.

### Procedural instanced draws

A procedural draw specifies the custom program, bound resources, vertex and
instance counts, and a conservative bound for culling. The application supplies
the vertex generation, storage-buffer layouts and any per-instance parameters.
It can bind same-Engine buffers produced by Vulkan or an external compute backend
through the existing resource and handoff contracts. No mesh binding is required.

Submit one instanced draw per visible batch in each applicable camera or shadow
pass. A zero count emits no draw. Count and binding changes take effect at frame
boundaries after validation; submitted frames retain the previous resources until
completion. Validate the reflected interface, buffer ownership, slice range and
alignment. The application must keep its count within the capacity of shader-indexed
streams. The renderer performs no per-instance CPU scan, format conversion or readback.
The initial count is CPU-supplied; indirect counts are outside v1.

### Reference sphere impostor shaders

The application example and the cloud benchmark use an opaque sphere impostor
with six procedurally generated vertices per instance. Their shared fixture uses
std430 arrays of 16-byte float records: center XYZ and radius, plus optional linear
RGBA in a separate array. Missing color is white. The application validates counts
against the streams, supplies finite centers and nonnegative finite radii, and
uses zero radius to disable a slot. The demo supports translation, rotation and
positive uniform scale for the visualization, independently of the simulation domain.

The camera and explicit shadow shaders use conservative quads and ray/sphere
intersections. Discard misses and write the projected hit depth. Use the hit
position and normal for lighting and shadow lookup. The sphere surface determines
occlusion in all views, including all six point-light faces. Avoid forcing early
fragment tests that bypass shader depth. Measure its depth cost and include
enlarged silhouette captures. The baseline uses normal fragment shading frequency;
MSAA does not guarantee sample-shaded analytic edges. See
[reference projection](#reference-sphere-projection-and-tests).

## First-version renderer contract

Byte layouts and behavior are contractual, subject to implementation probes;
public API names and signatures are selected during implementation.

### Draw interface and explicit host/shader layout

The primary renderer accepts application-supplied `RenderView` data without a
scene, tools or ECS dependency. An optional scene prepares the same draw data
and uses the same renderer. `RenderView` is a transient value containing one
camera, lights, and mesh/procedural draw descriptions with owning resource handles
and copied transforms. The renderer copies/retains everything needed for
submission before returning. It owns no ECS entity and does not serialize a
view. Both routes use the [frame](resources.md#frame-and-window-state) protocol
and require a valid camera.

Standard GPU blocks use 32-bit floats/uints, column-major matrices and the
chosen uniform or storage buffer layout. Define concrete bindings and packing
alongside the implemented renderer; verify host and shader offsets, strides and
device alignment. Reject incompatible reflected interfaces and retain resources
until GPU use completes. Do not require unused bindings or dummy resources
before their rendering paths exist. Runtime descriptor arrays and
update-after-bind are not required for the standard graphics path.

Frame data provides camera transforms, position, exposure, ambient light and the
counts needed by the active pass. Shadow passes use the light view's transforms
and position rather than the camera's. Exposure defaults to 1.0. Ambient
defaults to linear RGB (1,1,1) at intensity 0.06.

Light data supplies position, range, direction, cone, linear color, intensity
and shadow parameters as appropriate. Direction points from the light into the
scene. Intensity is a nonnegative linear multiplier. Range is finite positive
for local lights. Cone cosines satisfy inner >= outer. No fixed light array
appears in GLSL: the storage is sized to the count, with an explicit default
`maxLights=64` Engine budget configurable at creation up to checked device
buffer/descriptor limits. Exceeding the budget returns an error, never silently
drops lights. Each shadow view supplies a world-to-shadow transform and
normalized atlas region.

Point and spot direct illumination uses inverse-square distance attenuation with
a finite-range fade: `max(1 - (distance / range)^4, 0) / distance^2`. Spot
illumination also multiplies by the squared, clamped interpolation between the
cosines of its inner and outer cone angles. This follows the recommended
attenuation model
in [KHR_lights_punctual](https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_lights_punctual/README.md).
Scene point lights use the entity's world position; directional and spot lights
also use its local -Z direction transformed by the world rotation. Light range
and cone shape do not scale with the entity.

Material data provides linear base color and emissive, metallic, roughness,
occlusion strength, normal scale and alpha cutoff. Built-in default: tint white,
emissive black, metallic 0, roughness 1, occlusion strength 1, normal scale 1,
cutoff 0.5. Clamp validated metallic/roughness/strength to [0,1] and reject
non-finite inputs. A missing texture uses a semantic neutral value.

Draw data supplies a full model matrix, inverse-transpose normal transform and
linear tint. Select separate pipeline variants for single meshes and instanced
meshes before the draw; the first renderer needs only the single-mesh variant.
The instanced path reads per-instance data without a CPU update per vertex. The
standard CPU geometry helper stores `MeshVertex` records with a 64-byte stride:
position at offset 0, normal at 12, UV at 24, tangent at 32 and linear vertex
color at 48 (white by default). This convenience format is not required for
every mesh. `MeshDesc` can declare a different stride and a subset of attributes.
A nonempty layout requires Position; semantics must be unique, use their defined
float format, fit within the stride and not overlap. An empty layout creates
upload-only storage.

The current built-in renderer consumes Position for every draw, Normal for Lit
materials, Tangent when a usable Lit normal map is set, and optional Color for either
material model; a missing vertex color contributes white. It selects a compatible
pipeline from the material model, layout stride, consumed attribute offsets, face mode
and model determinant sign.
`RenderFace::Front`, `Back` and `Both` select the visible side (s); negative
determinants reverse the pipeline's front-face winding in both camera and shadow
passes. Lit shading flips the normal for rendered back faces in `Back` and `Both`
modes. Base-color, metallic-roughness, emissive and occlusion maps consume `TexCoord0`; a normal
map consumes `TexCoord0` and `Tangent` when both are present. Without those maps, UV
and tangent data are not required. A missing mesh normal remains a clear preflight
error for Lit materials.
Keep unused attributes out of vertex data. The current Unlit material uses base
color and emission without light evaluation. Emission only changes the surface's
own color; it does not illuminate other surfaces or create a bloom halo.
Procedural clouds without a vertex binding and instance-driven data remain outside
this implementation slice.

Current material parameters use one 64-byte std430 storage-buffer record per
draw: metallic at offset 0, roughness at 4, the Lit flag at 8, `RenderFace` at
12, alpha cutoff at 16, the base-color-map flag at 20, the metallic-roughness-map
flag at 24, the emissive-map flag at 28, and the occlusion-map flag at 48 with
occlusion strength at 52. A `vec4` at offset 32 stores emissive RGB and normal scale
in its fourth component. The record's final eight bytes are alignment padding.
Records are tightly packed per frame slot; the direct draw's
`firstInstance` supplies the flat material index to the fragment shader. Keep this
host/shader record layout synchronized.

### Materials, faces, and output

Built-in Lit uses a direct-light GGX/Smith/Schlick metallic/roughness BRDF with
dielectric F0=0.04, Lambert diffuse and material maps. It uses the supplied
environment for indirect lighting, or the simple ambient term when no
environment is supplied. Unlit uses base color and emission. Base color multiplies texture, material tint,
instance tint and vertex color (white when absent). The linear emissive factor
multiplies an optional sRGB emissive texture; its alpha is ignored. Emission is
added before tone mapping in both Lit and Unlit shading, but does not affect other
surfaces or the shadow pass. Base-color and emissive textures decode sRGB to
linear. Metallic/roughness, normal and occlusion textures decode linear. Metallic/roughness
texture uses B/G channels, occlusion R, and tangent-space normal XYZ. Normal-map XY
is multiplied by `normalScale` before tangent-to-world conversion. A normal map ignored
for a missing UV or tangent leaves the interpolated mesh normal unchanged. Missing
metallic/roughness uses (1,1,1), missing occlusion
uses white, and a missing emissive texture uses white (the default emissive factor
is black, so default emission remains black). Import computes missing normals and
tangents where UVs allow; if
UV/tangents are unusable it disables the normal map with a warning. Tangent `.w`
is handedness and bitangent is `cross(N,T)*tangent.w*sign(det(model))` after
normal-matrix transform and tangent orthogonalization. For a rendered back face,
flip N before forming the bitangent. The projection applies the Vulkan Y flip
once; with the positive-height viewport, counterclockwise front-face state
represents the outward-wound positive-determinant meshes used by Panda's geometry
helpers. A negative world determinant reverses front-face state in camera and
shadow passes. Face mode controls culling independently of the corrected
shading normal.

Use `R16G16B16A16_SFLOAT` HDR color and MSAA resolve attachments, checking
required format/sample/usage support. Prefer `D32_SFLOAT` camera/shadow depth,
allow `D16_UNORM` if sampled-depth/attachment capabilities support it, and
report the effective format. If no required HDR/depth format is usable, fail
Engine creation with a diagnostic. Tone-map by the fixed ACES fitted curve
`clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14),0,1)` per channel with exposure
1 then encode SDR sRGB. Prefer an SRGB swapchain format/colorspace, otherwise
UNORM plus explicit sRGB encoding; reject a surface lacking either supported SDR
route. UI overlays are composited in SDR after tone mapping. Clear HDR to (0.025,0.035, 0.05,1). Report effective
color/depth formats, sample count and
anisotropy.

### Shadows, quality, and pipelines

Use one sampled 2D-array depth image per frame slot; each 4096-square array layer
is an atlas page, and the image grows to the layer count required by the active
shadow plan. A shadowed directional/spot light uses one tile; point lights use six
cube-oriented views in six tiles and reconstruct the selected face in shader.
This implements the accepted six-view point-shadow behavior without requiring
descriptor-array indexing or cube-array support. Tiles store their layer index,
and each page is rendered through a 2D attachment view while the shader samples
the matching array layer. Pages are packed deterministically in 256-pixel cells
with a four-texel guard per tile; requested face resolutions are
256/512/1024/2048. Defaults: Studio directional 2048 with 30 m orthographic
width and near/far 0.1/80 m; direction normalized (-0.4,-1,-0.3), linear white
color and intensity 3; spot 1024; point faces 512. Invalid requests, unsupported
image dimensions or device array-layer limits return a clear error before
publishing settings. No silent resolution reduction or shadow-light dropping. A
one-pixel sampler footprint stays inside a guarded tile; clamps prevent PCF
leakage across neighbors. Use 3x3 PCF; initial depth bias 0.0005 and normal
offset 0.002 m are configurable starting values that require visual tuning, not
verified quality claims. Each built-in light has optional `ShadowSettings`;
absence disables its shadows, while an unset resolution selects the type's
default above. Directional lights also expose `shadowCoverageMeters`; spot and
point projection use their existing outer cone and range. Compare quality by
world-space texel density at the receiver: orthographic directional density is
uniform over its coverage, while perspective local-light density changes with
distance. Atlas rebuild/resize follows
[resource retirement rules](resources.md#frame-contexts-and-retirement). A
depth-only masked variant samples the base-color alpha with the same cutoff as
the camera pass. Blend casts no shadow by default.

Select MSAA 4x, then 2x, then off based on both actual HDR/depth format
capabilities; report the effective mode. Texture filtering uses the global
`TextureFilteringPreset`, with the requested preset and effective anisotropy
reported. Shadow and color format fallback is
capability driven, not a performance auto-tuner. The sphere edge remains a
documented masked silhouette.

A pipeline key includes shader revision/hash, vertex contract, pass kind,
descriptor layout signature, color/depth formats, sample count, alpha mode,
culling/front-face, and depth state. Material tints/roughness do not enter the
key. Use a Vulkan pipeline cache with a versioned application cache directory.
Before loading, check the Vulkan cache header size/version, vendor/device IDs
and pipelineCacheUUID against the selected physical device; an invalid or
corrupt cache is ignored. Save only on an administrative path via atomic file
replacement. Warmup covers known scene program and state combinations; first use
outside warmup is reported. Cache correctness never substitutes for the actual
key or shader-interface validation.

### Reference sphere projection and tests

In the GPU vertex stage, project the eight corners and near-plane-clipped edges
of each sphere's conservative view-space AABB, clamp the resulting screen rect
to the viewport, then draw its quad. No CPU particle traversal is involved. If
the view origin lies inside the sphere, use the full viewport rect. The fragment
ray tests both sphere roots and writes the nearest positive hit within the
pass's near/far clip range; misses discard. This applies to
orthographic/perspective camera and directional/spot/point shadow views. A
radius-zero slot discards. Verify the bound against a tessellated sphere across
off-axis, near-clipped and inside cases; do not rely on the old center-plane
square. The example's projection runs entirely on the GPU.

Acceptance requires reflected offset/stride tests, CPU projection/decomposition
tests, a GPU first cube with shadow, each light type, 65 lights with an enlarged
budget, camera/shadow alpha agreement, back-face/negative-scale/tangent checks,
tone-map and color-space captures, format/MSAA/AF fallback, wrong-cache
rejection, custom resource/shadow reload rejection, and sphere-versus-mesh depth
captures. Use RenderDoc traces and timestamps to verify draws and transitions.

## Required evidence

Test color-only/textured/instanced tint equivalence, color-space handling,
negative scale, both-sided normals, alpha clipping in camera and shadow passes,
and light counts above legacy fixed limits. Capture directional/spot/point
shadows, AA mode fallback, mip transitions, and custom-shader layout rejection.
Measure actual draw and submission counts for a GPU cloud rather than judging
performance visually.

References:
[Vulkan versions](https://docs.vulkan.org/guide/latest/versions.html),
[SPIRV-Reflect](https://github.com/KhronosGroup/SPIRV-Reflect),
[shader memory layout](https://docs.vulkan.org/guide/latest/shader_memory_layout.html),
[fragment/depth operations](https://docs.vulkan.org/spec/latest/chapters/fragops.html),
[pipeline caches](https://docs.vulkan.org/guide/latest/pipeline_cache.html),
[sampler configuration](https://docs.vulkan.org/refpages/latest/refpages/source/VkSamplerCreateInfo.html),
[glTF material conventions](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html).
