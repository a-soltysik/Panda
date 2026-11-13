# Materials, lights and shadows

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Implement per-mesh vertex layouts, standard material maps/alpha/faces, SSBO light
budget, spot/point atlas views, color space, tone map and pipeline keys.

Non-goal: No custom material DSL or IBL.

Interface: Per-light optional `ShadowSettings`, directional coverage, atlas views and renderer pipeline keys. A light
without shadow settings contributes direct light and allocates no atlas area.

Paths: `../../../include/core/panda`, `src/core/vulkan/renderer/`, `shaders/`, `tests/cases/`, examples and directly affected documentation.
Specifications: [Renderer layout and materials](../../design/contracts/rendering.md#first-version-renderer-contract), [transform conventions](../../design/contracts/scene.md#transforms-and-camera-conventions).

Verify material/host layouts first, then lighting/shadows, then quality and pipeline
behavior. Each slice needs its relevant visual or ABI evidence.

## Current implementation

`MeshDesc` declares per-mesh stride and standard semantic attributes; the renderer
selects Lit or Unlit pipelines and accepts compact Position/Color meshes. Lit draws
require Position and Normal. An optional, uploaded same-Context RGBA8 sRGB base-color
map requires sampled usage and `TexCoord0`; filtering is linear with repeat addressing.
Its color multiplies material and draw tint and optional vertex color, and its alpha
is used by Blend and by the matching Mask camera/shadow cutoff. Per-frame slots retain
all material maps through GPU completion. The Lit metallic-roughness map requires
sampled usage and `TexCoord0`; blue multiplies metallic and green multiplies roughness. It
does not affect alpha or shadow passes. A linear emissive factor defaults to black
and multiplies an optional sampled RGBA8 sRGB emissive map requiring `TexCoord0`;
emission is added before tone mapping for Lit and Unlit shading. It changes only
the object's visible color, not other surfaces, shadows or bloom. Lit materials can
use a sampled RGBA8 UNORM tangent-space normal map with `TexCoord0` and `Tangent`;
missing either attribute logs a warning and uses the interpolated mesh normal.
Lit materials can also use an optional sampled RGBA8 UNORM occlusion map requiring
`TexCoord0`; its red channel attenuates only ambient illumination, with a finite
`occlusionStrength` clamped to [0,1] when uploaded and defaulting to 1. The default
white fallback leaves ambient lighting unchanged.
`normalScale` defaults to 1, scales tangent-space X and Y, and accepts any finite value. Shader locations share one
preprocessor definition file consumed by C++ and GLSL, with C++ static assertions against
`VertexAttributeSemantic`. Material values use a named 64-byte record packed
contiguously in a per-frame storage buffer with one mapped write per frame. `Material`
selects front, back or both faces and Opaque, Mask or Blend alpha handling. Mask uses
the product of material alpha, draw tint alpha and vertex alpha (one when Color is
absent), with one cutoff in camera and shadow passes. Blend uses that product for
source-over compositing, draws after Opaque/Mask in mesh-origin back-to-front order
with depth writes disabled, and is skipped by the shadow pass. Camera and shadow
pipelines account for negative determinants, and Lit shading flips normals on
rendered back faces. `simple_scene` shows the compact layout, a cube with
base-color, metallic-roughness and emissive maps, a neighboring neutral cube with
normal and occlusion maps, and an untextured Unlit triangle; `scene_demo` keeps an upright,
vertex-colored triangle outside the rotating group so it does not collapse to an edge-on
streak, and lowers the unshadowed fill and ground tint to preserve shadow contrast. It
shows a shadowed directional light, directional fill, point light and spot light. The built-in renderer
packs all three light types in a per-frame SSBO. Point and spot contributions use
inverse-square distance attenuation with a finite-range fade; spot lights also fade
between their configured cone angles. `ContextOptions::maxLights` defaults to 64 and
is checked against device storage-buffer limits at Context creation; a view that
exceeds the budget returns `ResourceExhausted`. A zero budget supports views without
lights; Lit materials still receive the ambient term. Any directional, spot or point
light may opt into shadows. Each directional/spot light receives one guarded 2D atlas
tile; a point light receives six cube-oriented tiles. The atlas is fixed at 4096 by
4096 texels, with 256-texel allocation cells and 4-texel guards; `resolution` selects
the map size for each face, not the atlas size. Defaults are 2048 for directional with
30 m orthographic coverage, 1024 for spot, and 512 per point face. The renderer rejects
unsupported resolutions and atlas overflow without silently changing settings.
Masked shadows use the camera cutoff; blended draws do not cast. The local-light shadow
path and deterministic atlas allocation are implemented. RenderDoc captures and
optimized CPU scope timings are recorded below.

## Current verification

- Windows 11: `msvc-quality`, `all_verify_interface_header_sets`, `panda_format_checks`,
  `clang-cuda-analysis-windows`, and `msvc-fast` passed. Full CTest passed 623 tests;
  4 of 627 were skipped.
- WSL Ubuntu mirror: `gcc-quality`, interface-header and format targets,
  `clang-cuda-analysis`, `gcc-fast`, and `gcc-docs` passed. Full CTest passed 619 tests;
  8 of 627 were skipped. Both CUDA analyzers warn that the installed CUDA version is
  newer than their latest partially supported version, 12.9.
- `check_docs.py` checked 80 Markdown files and the task queue; `docs-checks` validated
  42 Mermaid diagrams in 83 files. No current coverage percentages are claimed because
  coverage was not rerun for this working tree.
- RenderDoc 1.46 captured separate 1280×720 `scene_demo` frames for directional, point
  and spot shadows on an RTX 4070 Ti. Each capture enabled only the light under review,
  making its shadow on the broad ground receiver visible. Defaults were directional
  2048, spot 1024, point 512 per face and 3×3 PCF. The 3×3 object layout and 14 m
  ground show receivers at different distances within the same view; this was a visual
  comparison, not a controlled camera-distance sweep or automated pixel baseline.
- MSVC 19.51 `RelWithDebInfo`, Ryzen 5 7600 and RTX 4070 Ti: with validation and external
  graphics hooks disabled, a 65-light `scene_demo` workload (directional, point and spot
  shadow casters plus 61 unshadowed lights) measured 500 frames after 100 warmup frames.
  `Scene::createRenderView` averaged 14.5 μs (11.2–54.8 μs); `MeshRenderer::record`
  averaged 43.8 μs (34.2–89.0 μs). These direct scope timings exclude presentation
  waits; they showed no renderer-stage cost that justified an optimization. Nsight CPU
  stack sampling was unavailable because it requires administrator privileges.
- The separate 65-light smoke benchmark completed 720 samples at 320×240 with validation
  and the external profiler disabled: CPU frame time averaged 6.93 ms (p95 7.01 ms),
  GPU execution averaged 91 μs (p95 117 μs). FIFO presentation dominates total CPU frame
  time, so this result is not used to rank renderer hotspots.

## Acceptance

ABI/visual cases for position/color-only and lit mesh layouts, emissive factor and
map behavior in Lit and Unlit materials, blend compositing and draw order, missing
tangent with normal-map fallback, 65 lights with an enlarged budget, mask shadows,
negative scale, tangent sign, point/spot range and cone lighting, spot/point shadows
and format fallback.

CPU performance evidence: profile render-view preparation and command recording in
an optimized build for the 65-light case and representative spot/point shadow
casters. Disable validation and external graphics hooks for timing, record the
measured hotspots, and optimize only costs confirmed in those workloads.

The shadow atlas uses 3x3 PCF with initial configurable depth bias 0.0005 and normal
offset 0.002 m. Quality depends on projected texel density, not face resolution alone:
directional density is uniform over its configured width, while point-light density
falls with distance. Compare receivers at representative distances for every light
type. GPU captures must show usable shadow detail without visible self-shadow acne or
detached shadows; record the effective resolution/filtering and remaining limits.

Learning: Inspect a tinted textured surface and its shadow in RenderDoc.
