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
the object's visible color, not other surfaces, shadows or bloom. Shader locations
share one preprocessor definition file consumed by C++ and GLSL, with C++ static assertions against
`VertexAttributeSemantic`. Material values use a named 48-byte record packed
contiguously in a per-frame storage buffer with one mapped write per frame. `Material`
selects front, back or both faces and Opaque, Mask or Blend alpha handling. Mask uses
the product of material alpha, draw tint alpha and vertex alpha (one when Color is
absent), with one cutoff in camera and shadow passes. Blend uses that product for
source-over compositing, draws after Opaque/Mask in mesh-origin back-to-front order
with depth writes disabled, and is skipped by the shadow pass. Camera and shadow
pipelines account for negative determinants, and Lit shading flips normals on
rendered back faces. `simple_scene` shows the compact layout, the cube's
base-color, metallic-roughness and emissive maps, and an untextured Unlit triangle;
`scene_demo` puts an upright, vertex-colored triangle into the existing animated
object grid.

Normal and occlusion maps, multiple lights, spot/point
shadows, normal-map fallback, and the required shadow-quality captures remain incomplete.
This task is not accepted.

## Current verification

- Windows 11: `cmake --preset msvc-quality` and its full build passed, as did
  `all_verify_interface_header_sets panda_format_checks`. The
  `clang-cuda-analysis-windows` configure/build passed. The `msvc-fast`
  configure/build and full CTest passed: 533 cases, four skipped (one benchmark
  and three noncoherent-memory variants). Tool versions: CMake 4.4.2, Ninja
  1.13.2, ccache 4.13.6.
- WSL Ubuntu 26.04: after synchronization with
  `.agents/skills/panda-dual-platform-build/scripts/build-wsl.ps1 -SyncOnly`,
  `gcc-quality` and the `all_verify_interface_header_sets panda_format_checks`
  targets passed. The `clang-cuda-analysis` configure/build passed. The
  `gcc-fast` configure/build and full CTest passed: 533 cases, eight skipped
  (five CUDA capability cases and three noncoherent-memory variants). The
  `gcc-docs` configure/build generated the public API documentation. Tool
  versions: CMake 4.2.3, GCC 16.0.1, Ninja 1.13.2, ccache 4.12.3.
- Clang CUDA analysis on both platforms reports that the installed CUDA version
  is newer than Clang's latest partially supported version 12.9; the configured
  analysis targets still pass.
- `git diff --check` passed. `check_docs.py` checked 80 Markdown files and the
  task queue with no structural errors; `docs-checks` validated 42 Mermaid
  diagrams in 83 files. SPIR-V disassembly confirms a 48-byte `MaterialRecord`
  array stride in both
  mesh and shadow fragment shaders.
- Windows and Linux windowed Vulkan system cases submit Lit and Unlit emissive
  draws and pass, but they do not compare pixels. An attempt to inspect the
  launched `simple_scene` window did not expose it through the available window
  inventory, so the emissive appearance has not been visually confirmed in this
  run.
- No RenderDoc shadow-quality capture or pixel comparison was made; the task
  remains in progress.

## Acceptance

ABI/visual cases for position/color-only and lit mesh layouts, emissive factor and
map behavior in Lit and Unlit materials, blend compositing and draw order, missing
tangent with normal-map fallback, 65 lights, mask shadows, negative scale, tangent
sign, spot/point shadows and format fallback.

The first-scene shadow uses a fixed 2048 by 2048 map over 30 m and 3x3 PCF, which
visibly aliases at its edges. This task must tune or replace that filtering and
coverage strategy. GPU captures at representative camera distances must show a
clear improvement without visible self-shadow acne or detached shadows; record the
effective resolution/filtering and any remaining limits.

Learning: Inspect a tinted textured surface and its shadow in RenderDoc.
