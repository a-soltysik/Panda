# Materials, lights and shadows

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Implement per-mesh vertex layouts, standard material maps/alpha/faces, SSBO light
budget, spot/point atlas views, color space, tone map and pipeline keys.

Non-goal: No custom material DSL or IBL.

Interface: Material and light data, shadow views and renderer pipeline keys; finalize public names and GPU layout during
implementation.

Paths: `../../../include/core/panda`, `src/renderer/`, `shaders/`, `tests/gpu/` and directly affected documentation.
Specifications: [Renderer layout and materials](../../design/contracts/rendering.md#first-version-renderer-contract), [transform conventions](../../design/contracts/scene.md#transforms-and-camera-conventions).

Verify material/host layouts first, then lighting/shadows, then quality and pipeline
behavior. Each slice needs its relevant visual or ABI evidence.

## Current implementation

`MeshDesc` declares per-mesh stride and standard semantic attributes; the renderer
selects Lit or Unlit pipelines and accepts compact Position/Color meshes. Lit draws
require Position and Normal. Material values are stored as named 16-byte records,
packed contiguously in a per-frame storage buffer with one mapped write per frame.
`simple_scene` shows the compact layout and an Unlit draw; `scene_demo` puts a
upright, vertex-colored triangle into the existing animated object grid.

Maps, alpha and face modes, multiple lights, spot/point shadows, normal-map fallback,
and the required shadow-quality captures remain incomplete. This task is not accepted.

## Current verification

- Windows presets: `msvc-quality`, `clang-cuda-analysis-windows`, format and public
  header checks passed; `msvc-fast` built and CTest ran 530 cases with zero failures
  and 4 skipped (one benchmark and three noncoherent-memory variants). Tool versions:
  CMake 4.4.2, Ninja 1.13.2 and ccache 4.13.6.
- WSL Ubuntu presets: `gcc-quality`, `clang-cuda-analysis`, format and public header
  checks passed; `gcc-fast` built and CTest ran 530 cases with zero failures and 8
  skipped (CUDA interop/handoff and noncoherent-memory variants). `gcc-docs`
  generated API docs. Tool versions: CMake 4.2.3, GCC 16.0.1, Clang 22.1.2 and
  ccache 4.12.3.
- After the final Doxygen comment edits, `check_docs.py` checked 79 Markdown files;
  `docs-checks` accepted 42 Mermaid diagrams in 82 files; Windows format/header
  checks, Linux API docs and `git diff --check` passed. Both complete platform
  quality/CUDA-analysis/fast sequences were rerun on the final feature branch;
  Clang reported that CUDA 13.1 is newer than its latest partially supported 12.9.
- No GPU screenshot or RenderDoc capture was inspected. The demo triangle's placement
  and orientation were checked in source, not visually validated.

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
