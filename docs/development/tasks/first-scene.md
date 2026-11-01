# First lit and shadowed rendering

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Add direct application-supplied render data, primitives, built-in material,
directional light/shadow, MSAA fallback and a public-API cube example without a
scene or ECS dependency. The existing `simple_scene` example may use Tools for
its window; the renderer itself does not require Tools. The application owns its
state and update loop.

Non-goal: No point shadows, custom shader or asset import.

Interface: renderer draw descriptions and a render view; finalize signatures during implementation.

Paths: `include/core/panda/`, `src/renderer/`, `shaders/`, examples, tests and directly affected documentation.
Specifications: [First scene](../../design/contracts/scene.md#first-useful-scene), [renderer](../../design/contracts/rendering.md#first-version-renderer-contract), [resource lifetime](../../design/contracts/resources.md#frame-contexts-and-retirement).

## Acceptance

Use the existing `simple_scene` example to render a cube without a scene or ECS;
verify the renderer can be consumed without Tools. Capture the cube, ground,
shadow and effective quality with GPU validation on supported devices.

Learning: Explain the default light, shadow depth and effective MSAA choice.

## Current work

Checked perspective and orthographic projection matrices and CPU unit-cube
geometry are available without a scene. The shared CPU Geometry owns the standard
64-byte vertex format and optional 16- or 32-bit indices. Context creates matching
mesh storage; UploadBatch copies Geometry into upload staging. `simple_scene`
creates and uploads the cube through those APIs, while the lower-level upload path
continues to accept raw application-defined data. The example still clears the
window without drawing the cube.
Rendering, other primitives, materials, lights, shadows, MSAA and visible cube
drawing remain to be implemented and verified.

## Current verification

- Windows MSVC 14.51.36231: `msvc-quality`, header isolation, format checks,
  `clang-cuda-analysis-windows` and `msvc-fast` passed. The full suite ran 465
  cases: 461 passed; one buffered-output case and three noncoherent-memory cases
  skipped.
- WSL Ubuntu GCC 16.0.1: `gcc-quality`, header isolation, format checks,
  `clang-cuda-analysis`, `gcc-fast` and `gcc-docs` passed. The full suite ran 465
  cases: 457 passed; five CUDA-capability and three noncoherent-memory cases
  skipped. Clang reported the installed CUDA version is newer than its latest
  partially supported version (12.9); host and device analysis passed.
- `check_docs.py` checked 76 Markdown files and the task queue. `docs-checks`
  validated 41 diagrams in 77 files. GCC quality reported non-fatal indexing and
  magic-number advisories in the new cube geometry and tests.
- No rendered-image validation was performed. Renderer drawing and the task's
  visual acceptance remain open.
