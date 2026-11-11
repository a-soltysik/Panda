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
vertex-colored triangle into the existing animated object grid and shows a first,
shadowed directional light with an unshadowed fill light. The built-in renderer packs
all directional-light records in a per-frame SSBO. `ContextOptions::maxLights` defaults
to 64 and is checked against device storage-buffer limits at Context creation; a view
that exceeds the budget returns `ResourceExhausted`. A zero budget supports views
without directional lights; Lit materials still receive the ambient term. The first
directional light uses the current shadow map, and later lights are unshadowed.

Spot/point lighting and shadows, and the required shadow-quality captures remain
incomplete.
This task is not accepted.

## Current verification

- Windows 11: `msvc-quality` build and the `all_verify_interface_header_sets` and
  `panda_format_checks` targets passed. `clang-cuda-analysis-windows` configured
  and built successfully. `msvc-fast` configured and built; `ctest --preset msvc-fast`
  passed all 608 tests, with four skips (one benchmark and three noncoherent-memory
  variants). Tool versions: CMake 4.4.2, Ninja 1.13.2, ccache 4.13.6.
- WSL Ubuntu 26.04: after syncing with
  `.agents/skills/panda-dual-platform-build/scripts/build-wsl.ps1 -SyncOnly`,
  `gcc-quality` build and the `all_verify_interface_header_sets` and
  `panda_format_checks` targets passed. `clang-cuda-analysis` configured and built
  successfully. `gcc-fast` configured and built; `ctest --preset gcc-fast` passed
  all 608 tests, with eight skips (five CUDA capability cases and three
  noncoherent-memory variants). `gcc-docs` generated the public API documentation.
  Tool versions: CMake 4.2.3, GCC 16.0.1, Ninja 1.13.2, ccache 4.12.3.
- The system tests render the 65-directional-light budget and zero-light Unlit cases.
  They check submission and validation behavior, not expected pixels. The occlusion
  map system case also checks upload/model/format/UV/usage rejection, not pixel
  values. SPIR-V disassembly confirms a 64-byte `MaterialRecord` array stride and
  its member offsets in the mesh and shadow fragment shaders.
- `git diff --check` passed. `check_docs.py` checked 80 Markdown files and the task
  queue with no structural errors. `docs-checks` and the API documentation build
  passed for the current source.
- No visual inspection or pixel comparison was made for the current changes, and
  there is no RenderDoc shadow-quality capture or CPU performance profile. Spot/point
  lighting and shadows and shadow-quality evidence remain incomplete.

## Acceptance

ABI/visual cases for position/color-only and lit mesh layouts, emissive factor and
map behavior in Lit and Unlit materials, blend compositing and draw order, missing
tangent with normal-map fallback, 65 lights with an enlarged budget, mask shadows,
negative scale, tangent sign, spot/point shadows and format fallback.

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
