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
require Position and Normal. Material values use a named 16-byte record packed
contiguously in a per-frame storage buffer with one mapped write per frame. `Material`
selects front, back or both faces; camera and shadow pipelines account for negative
determinants, and Lit shading flips normals on rendered back faces. `simple_scene`
shows the compact layout and an Unlit draw; `scene_demo` puts an upright,
vertex-colored triangle into the existing animated object grid.

Maps and alpha modes, multiple lights, spot/point shadows, normal-map fallback,
and the required shadow-quality captures remain incomplete. This task is not accepted.

## Current verification

- Windows 11: `msvc-quality`, public-header and format checks passed. `msvc-fast`
  built all targets and CTest ran 530 cases with zero failures and 4 skips (one
  benchmark and three noncoherent-memory variants). The windowed
  `RendersFaceModesAndReflectedMeshesWithValidation` test passed with Vulkan
  validation. Tool versions: CMake 4.4.2, Ninja 1.13.2 and ccache 4.13.6.
- WSL Ubuntu 26.04: `gcc-quality` built all targets. The synchronized
  `.agents/skills/panda-dual-platform-build/scripts/build-wsl.ps1` `gcc-fast` run
  built all targets and CTest ran 530 cases with zero failures and 8 skips (five
  CUDA capability cases and three noncoherent-memory variants). The same windowed
  face/reflection test passed. Tool versions: CMake 4.2.3, GCC 16.0.1 and ccache
  4.12.3.
- `check_docs.py` checked 79 Markdown files; `docs-checks` accepted 42 Mermaid
  diagrams in 82 files; `git diff --check` passed. CUDA-analysis and API-doc
  presets were not rerun after this slice.

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
