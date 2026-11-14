# First lit and shadowed rendering

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Add direct application-supplied render data, primitives, built-in material,
directional light/shadow, MSAA fallback and a public-API cube example without a
`Scene` class, scene-management dependency or ECS. The existing `simple_scene`
example may use Tools for its window; the renderer itself does not require Tools.
The application owns its state and update loop.

Non-goal: No point shadows, custom shader or asset import.

This task implements the first direct `RenderView` mesh-draw path. Keep public names
and ownership rules with the declarations; this contract defines rendering behavior,
not a planned scene API.

Paths: `include/core/panda/`, `src/core/vulkan/`, `shaders/`, examples, tests and directly affected documentation.
Specifications: [First scene](../../design/contracts/scene.md#first-useful-scene), [renderer](../../design/contracts/rendering.md#first-version-renderer-contract), [resource lifetime](../../design/contracts/resources.md#frame-contexts-and-retirement).

## Acceptance

Use the existing `simple_scene` example to render a cube from an application-owned
`MeshDraw` collection, without a `Scene` class or ECS; verify the renderer can be
consumed without Tools. Capture the cube, ground, shadow and effective quality with
GPU validation on supported devices.

Learning: Explain the default light, shadow depth and effective MSAA choice.

## Current work

Checked perspective and orthographic projection matrices and CPU unit-cube
geometry are available without a scene. The shared CPU Geometry owns the standard
64-byte vertex format and optional 16- or 32-bit indices. Context creates matching
mesh storage; UploadBatch copies Geometry into upload staging. `WindowRenderer::render(view)`
accepts application camera, directional-light and mesh-draw values, validates mesh
ownership and each draw's required attributes, and retains mesh handles through GPU completion. The
first built-in renderer draws uploaded meshes with a directional lit material and a
depth attachment. An empty view presents the background. `simple_scene` uploads and
draws its cube and 10 m ground plane plus a compact Position/Color triangle, without
a scene or ECS dependency. All three meshes use one upload batch; the plane is an
ordinary application-supplied draw using the public unit-plane geometry helper and
`panda::createModelMatrix()` for its TRS transform.
The renderer records a depth-only directional-light
pass, then a camera color/depth pass that samples a per-frame-slot 2048 by 2048 depth
map with 3x3 PCF. The shadow view is camera-centered with a 30 m orthographic width
and 0.1-80 m depth range. Receiver bias increases toward grazing angles to reduce
self-shadow acne. The example places the directional light opposite the camera so
the cube's shadow falls onto visible ground. The fixed coverage and narrow PCF filter
still produce visible edge aliasing; shadow coverage, filtering and bias quality are
follow-up work in [Materials, lights and shadows](materials-and-shadows.md), not the
final quality target. When the window framebuffer is zero-sized, the example waits
through Tools without calling `render`; Core still
returns `Suspended` if the extent changes after the application's check. The benchmark
smoke workload likewise uploads one cube during setup and reuses a static render view.
For mesh draws, the renderer chooses the highest common color/depth sample count supported
by the actual output formats and device limits (4x, 2x, then 1x); `FrameResult` and smoke
benchmark reports expose the effective count.

The system test runs both passes for cube, ground and a compact Position/Color triangle under Vulkan validation and checks
that the reported count is 1x, 2x or 4x. The maintainer confirms the current `simple_scene`
renders correctly with its symmetric 3x3 PCF kernel. The screenshot shared during filter
tuning showed the cube, ground and shadow, but is not stored in the repository. No RenderDoc
capture was available.

## Current verification

- Windows 11, MSVC 14.51.36231, CMake 4.4.2, ccache 4.13.6 and Vulkan SDK 1.4.341.1
  passed `msvc-quality`, public-header and format targets, `clang-cuda-analysis-windows`,
  `msvc-fast` and the full CTest suite. The suite ran 491 cases: 487 passed and four
  skipped (one buffered-output case and three noncoherent-memory variants). The shader
  build used glslc and spirv-val 2026.1. The windowed cube-and-ground test passed with
  Vulkan validation and checked the effective sample count and unused-attribute warnings.
- WSL Ubuntu 26.04, GCC 16.0.1, CMake 4.2.3, ccache 4.12.3, glslc 2026.1-1 and
  SPIRV-Tools 2026.1 passed `gcc-quality`, public-header and format targets,
  `clang-cuda-analysis`, `gcc-fast`, the full CTest suite and `gcc-docs`. The suite ran
  491 cases: 483 passed and eight skipped (five CUDA/device-capability cases and three
  noncoherent-memory variants). The windowed cube-and-ground test passed with Vulkan
  validation. Both platforms used the checked-in incremental preset builds with ccache.
- `check_docs.py` checked 77 Markdown files and the task queue; `docs-checks` validated
  41 diagrams in 79 files. The maintainer confirms the rendered scene works with the
  symmetric 3x3 filter. The screenshot shared during filter tuning is not stored in the
  repository; no RenderDoc capture was available, so there is no stored image evidence.
