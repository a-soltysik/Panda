# Materials, lights and shadows

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Implement per-mesh vertex layouts, standard material maps/alpha/faces, SSBO light
budget, spot/point atlas views, color space, tone map and pipeline keys.

Non-goal: No custom material DSL or IBL.

Interface: Material and light data, shadow views and renderer pipeline keys; finalize public names and GPU layout during
implementation.

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
normal and occlusion maps, and an untextured Unlit triangle; `scene_demo` puts an upright,
vertex-colored triangle into the existing animated object grid and shows a shadowed
directional light, directional fill, point light and spot light. The built-in renderer
packs all three light types in a per-frame SSBO. Point and spot contributions use
inverse-square distance attenuation with a finite-range fade; spot lights also fade
between their configured cone angles. `ContextOptions::maxLights` defaults to 64 and
is checked against device storage-buffer limits at Context creation; a view that
exceeds the budget returns `ResourceExhausted`. A zero budget supports views without
lights; Lit materials still receive the ambient term. The first directional light
uses the current shadow map; other directional lights and all point/spot lights are
unshadowed.

Spot/point shadowing and the required shadow-quality captures remain incomplete.
This task is not accepted.

## Current verification

- Windows 11: `msvc-quality` build, `all_verify_interface_header_sets`,
  `panda_format_checks`, `clang-cuda-analysis-windows`, `msvc-fast` build and full
  CTest passed; CTest reported 617 passed and 4 skipped.
- WSL Ubuntu mirror: `gcc-quality` build, interface-header and format targets,
  `clang-cuda-analysis`, `gcc-fast` build and full CTest passed; CTest reported 617
  passed and 8 skipped. Both CUDA analyzers warned that the installed CUDA version
  is newer than their latest partially supported version, 12.9.
- The GCC coverage CTest run passed 600 cases and skipped 4. Its report covers only
  engine sources under `src/`: 95.8% lines (6115/6386), 98.0% functions (1010/1031)
  and 86.4% branches (5073/5872). The separate lavapipe benchmark smoke run is not
  included in that report.
- `check_docs.py` checked 80 Markdown files and the task queue; `docs-checks` validated
  42 Mermaid diagrams in 83 files, and `gcc-docs` passed. No RenderDoc capture or
  pixel comparison was performed in this verification.
- Spot/point shadows, directional shadow-quality captures and CPU performance
  profiling remain incomplete.

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

The first-scene shadow uses a fixed 2048 by 2048 map over 30 m and 3x3 PCF, which
visibly aliases at its edges. This task must tune or replace that filtering and
coverage strategy. GPU captures at representative camera distances must show a
clear improvement without visible self-shadow acne or detached shadows; record the
effective resolution/filtering and any remaining limits.

Learning: Inspect a tinted textured surface and its shadow in RenderDoc.
