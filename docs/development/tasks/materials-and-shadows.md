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
require Position and Normal. Material values use a named 32-byte record packed
contiguously in a per-frame storage buffer with one mapped write per frame. `Material`
selects front, back or both faces and Opaque or Mask alpha handling. Mask uses the
product of material alpha, draw tint alpha and vertex alpha (one when Color is
absent), with one cutoff in camera and shadow passes. Camera and shadow pipelines
account for negative determinants, and Lit shading flips normals on rendered back
faces. `simple_scene` shows the compact layout and an Unlit draw; `scene_demo` puts an
upright, vertex-colored triangle into the existing animated object grid.

Material maps, Blend alpha handling, multiple lights, spot/point shadows,
normal-map fallback, and the required shadow-quality captures remain incomplete.
This task is not accepted.

## Current verification

- Windows 11: `msvc-quality`, isolated public headers, format checks and
  `clang-cuda-analysis-windows` passed. `msvc-fast` full build passed; CTest
  passed 533/533 with 4 skips (one benchmark and three noncoherent-memory
  variants). Tool versions: CMake 4.4.2, Ninja 1.13.2, ccache 4.13.6.
- WSL Ubuntu 26.04, synchronized with
  `.agents/skills/panda-dual-platform-build/scripts/build-wsl.ps1`: `gcc-quality`,
  isolated public headers, format checks, `clang-cuda-analysis` and `gcc-docs`
  passed. `gcc-fast` full build passed; CTest passed 533/533 with 8 skips (five
  CUDA capability cases and three noncoherent-memory variants). Tool versions:
  CMake 4.2.3, GCC 16.0.1, Ninja 1.13.2, ccache 4.12.3.
- `gcc-coverage-all` with Mesa lavapipe passed 516/516 with 4 hardware-dependent
  skips. Overall coverage: 89.7% lines, 94.5% functions and 73.8% branches.
  `MeshRenderer.cpp`: 90.4% lines, 59.1% branches;
  `RenderViewValidation.cpp`: 100% lines, 92.9% branches.
- Captures of the running Windows `simple_scene` and `scene_demo` windows after
  the front-face correction show the ground and colored triangle again. This is
  a visual smoke check, not an automated pixel comparison or shadow-quality
  assessment; the windowed system test checks presentation and Vulkan validation.
- The windowed system test runs Mask draws with and without a Color vertex
  attribute through camera and shadow passes, then checks presentation and
  absence of Vulkan validation errors. It does not compare pixels.
  `check_docs.py` checked 79 Markdown files; `docs-checks` accepted 42 Mermaid
  diagrams in 82 files. No RenderDoc shadow-quality capture was made.

## Acceptance

ABI/visual cases for position/color-only and lit mesh layouts, missing tangent with
normal-map fallback, 65 lights, mask shadows, negative scale, tangent sign,
spot/point shadows and format fallback.

The first-scene shadow uses a fixed 2048 by 2048 map over 30 m and 3x3 PCF, which
visibly aliases at its edges. This task must tune or replace that filtering and
coverage strategy. GPU captures at representative camera distances must show a
clear improvement without visible self-shadow acne or detached shadows; record the
effective resolution/filtering and any remaining limits.

Learning: Inspect a tinted textured surface and its shadow in RenderDoc.
