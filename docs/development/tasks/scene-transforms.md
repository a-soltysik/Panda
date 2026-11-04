# Scene identities and transforms

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Build `Panda::Scene`, an optional, GUI-independent prototype scene on the direct renderer. Implement
checked object identity, TRS hierarchy and world caching; camera projection belongs
to the renderer and is reusable without this scene.

Non-goal: No renderer materials, importer or physics.

Interface: `Panda::Scene` and checked object/transform operations; finalize API
names during implementation. No public ECS or EnTT requirement.

Paths: optional scene library headers and implementation, `tests/cases/unit/` and directly affected documentation.
Specifications: [Scene API and transforms](../../design/contracts/scene.md#public-scene-boundary), [ownership](../../design/contracts/resources.md#ownership).

Separate identity/lifecycle and transform math into reviewable slices with focused CPU tests.

## Acceptance

CPU stale-handle, reparent/shear and invalid value tests; exercise the scene without
GUI and ensure direct rendering remains usable without the scene library.

Learning: Explain a rotated child under nonuniform parent scale and a rejected keep-world edit.

## Current work

The implementation adds optional `Panda::Scene` with C++ API in `panda::scene`, move-stable runtime
handles, object names, checked local/world TRS edits, parent changes and lazy world
matrix caching. The cache preserves affine shear for rendering; keep-world edits
reject a result that cannot be represented by local TRS. Scene objects may retain
meshes, perspective camera lenses and directional-light parameters. A selected
camera and visible objects produce a `RenderView` snapshot; camera/light
poses reject shear, reflections and nonuniform scale. Deleting the active camera
clears selection. Visibility does not hide children. One visible directional light
is supported; more report `Unsupported`, and no light produces zero direct intensity.
Renderer-specific data and mesh ownership are checked once in `Context::render()`.
The `scene_demo` example renders an animated hierarchy through this API.
`simple_scene` remains a separate direct-renderer example without Scene.

Stable document IDs, Studio defaults, orthographic scene lenses and primitive
helpers remain unfinished. Persistence and asset integration remain in their later task.

## Current verification

- CPU cases cover stale/cross-scene handles, slot reuse, cycles, deletion, dirty
  caches, reflected transforms, world edits under translated/rotated parents,
  keep-world/shear failure atomicity, invalid values, camera/lens restrictions,
  light transforms and visibility. Headless public-API
  cases cover Scene moves, snapshot mesh retention and renderer rejection of a
  foreign mesh before submission.
- Windows, MSVC 14.51.36231, CMake 4.4.2, LLVM 22.1.8 and ccache 4.13.6 passed
  `msvc-quality`, `all_verify_interface_header_sets`, `panda_format_checks`,
  `clang-cuda-analysis-windows`, `msvc-fast` and the full CTest suite: 511 cases,
  507 passed and four skipped
  (one buffered-output case and three noncoherent-memory variants). Configuring
  `msvc-fast` with `PANDA_BUILD_SCENE=OFF` also built all enabled targets, including
  `simple_scene`, and ran all 495 registered cases: 491 passed and four skipped.
  The final fast configuration has Scene enabled again.
- The synchronized WSL Ubuntu mirror, GCC 16.0.1, CMake 4.2.3, LLVM 22.1.2 and
  ccache 4.12.3 passed `gcc-quality`, public-header and format targets,
  `clang-cuda-analysis`, `gcc-fast`, the full CTest suite and `gcc-docs`. Of 511
  cases, 503 passed and eight skipped
  (five CUDA/device-capability cases and three noncoherent-memory variants).
  Both platforms used the prescribed incremental presets with ccache enabled.
- `check_docs.py` checked 78 Markdown files and the task queue; `docs-checks`
  validated 42 diagrams in 81 files. The scene flow and example guide describe
  the current API and separate parent rotation from per-object wave animation.
- The maintainer visually reviewed and accepted the animated `scene_demo`; no
  capture was recorded. Sanitizer profiles were not run.
