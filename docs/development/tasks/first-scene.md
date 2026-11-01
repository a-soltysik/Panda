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
mesh storage; UploadBatch copies Geometry into upload staging. `Context::render(view)`
accepts application camera, directional-light and mesh-draw values, validates mesh
ownership and standard stride, and retains mesh handles through GPU completion. The
first built-in renderer draws uploaded meshes with a directional lit material and a
depth attachment. An empty view presents the background. `simple_scene` now uploads
and draws its cube without a scene or ECS dependency. When the window framebuffer is
zero-sized, the example waits through Tools without calling `render`; Core still
returns `Suspended` if the extent changes after the application's check. The benchmark
smoke workload likewise uploads one cube during setup and reuses a static render view.

Ground geometry, shadow mapping, MSAA selection/reporting, visual GPU capture and
effective-quality evidence remain outstanding for this task's acceptance.

## Current verification

- Windows: MSVC 14.51.36231 and Vulkan SDK 1.4.341.1. `msvc-quality`,
  `all_verify_interface_header_sets`, `panda_format_checks`,
  `clang-cuda-analysis-windows`, `msvc-fast` and `ctest --preset msvc-fast` passed.
  The suite ran 475 cases: 471 passed and four skipped (one buffered-output case and
  three noncoherent-memory cases). ccache was enabled.
- WSL: Ubuntu 26.04 LTS, GCC 16.0.1, CMake 4.2.3, `glslc` shaderc 2026.1-1 and
  `spirv-val` SPIRV-Tools 2026.1. `gcc-quality`,
  `all_verify_interface_header_sets`, `panda_format_checks`,
  `clang-cuda-analysis`, `gcc-fast`, `ctest --preset gcc-fast` and `gcc-docs` passed.
  The suite ran 475 cases: 467 passed and eight skipped (five CUDA-capability cases
  and three noncoherent-memory cases). ccache was enabled. CUDA analysis passed for
  host and device code; Clang warned that the installed CUDA version is newer than
  its latest partially supported 12.9.
- `python .agents/skills/panda-project-maintenance/scripts/check_docs.py` checked
  77 Markdown files and the task queue. `docs-checks` validated 41 diagrams in 79
  files, and `gcc-docs` generated the public API documentation.
- `WindowedContextSystem.RendersUploadedCubeWithValidation` passed on both platforms;
  it uploads and draws a cube, waits for GPU completion, destroys its Context, then
  checks captured logs for Vulkan validation errors. No visual image capture was
  made, so pixel appearance is not established. Ground geometry, shadows, MSAA
  selection/reporting and visual acceptance remain open.
