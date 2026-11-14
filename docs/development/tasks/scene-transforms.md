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

The optional `Panda::Scene` API lives in `panda::scene`. It starts empty and provides
move-stable runtime handles, scene-lifetime 64-bit object IDs, names, checked
local/world TRS edits, parent changes and lazy world-matrix caching. The cache
preserves affine shear for rendering; keep-world edits reject a result that cannot
be represented by local TRS. Objects may retain meshes, perspective or orthographic
camera lenses, and directional, point or spot light parameters. The application
creates entities, attaches components, selects a camera, and owns geometry and
uploads. A selected camera and visible objects produce a `RenderView` snapshot;
camera and light poses reject shear, reflections and nonuniform scale. Point lights
emit from the entity's world position. Deleting the active camera clears selection.
Visibility does not hide children. Visible lights are copied in scene
order to `RenderView::lights`. Each light may opt into shadows through
`ShadowSettings`; without it the light contributes direct lighting only. Directional
coverage is explicit, and local views follow light range and spot cone. With no
visible lights, Lit materials retain only their ambient contribution.
Renderer-specific data and mesh ownership are checked once in `WindowRenderer::render()`.
Core provides configurable CPU sphere geometry. The `scene_demo` example renders an
animated hierarchy through Scene; `simple_scene` remains a direct-renderer example.
Scene serialization and asset integration remain in their later tasks; no ID
encoding or serialization interface is implemented yet.

## Current verification

- CPU cases cover ObjectId uniqueness and duplicate rejection, stale/cross-scene
  handles, slot reuse, cycles, deletion, dirty caches, reflected transforms, world
  edits under translated/rotated parents, keep-world/shear failure atomicity,
  invalid values, perspective and orthographic cameras, light transforms,
  visibility, and configurable sphere geometry. Headless public-API cases cover
  Scene moves and destruction, snapshots, object-ID lookup, orthographic cameras,
  component forwarding, and foreign-mesh rejection. Coverage leaves one private
  missing-object-ID invariant error path unexecuted; public operations cannot create
  that invalid slot state.
- Windows, MSVC 14.51.36231, CMake 4.4.2 and ccache 4.13.6 passed
  `cmake --preset msvc-quality`, `cmake --build --preset msvc-quality`,
  `cmake --build --preset msvc-quality --target all_verify_interface_header_sets panda_format_checks`,
  `cmake --preset clang-cuda-analysis-windows`,
  `cmake --build --preset clang-cuda-analysis-windows`, `cmake --preset msvc-fast`,
  `cmake --build --preset msvc-fast --parallel` and `ctest --preset msvc-fast`.
  All 525 cases passed or skipped: 521 passed and four skipped (buffered-output and
  three noncoherent-memory variants). CUDA analysis reported that the installed
  CUDA version is newer than Clang's latest partially supported version (12.9).
- The synchronized WSL Ubuntu mirror, GCC 16.0.1, CMake 4.2.3 and ccache 4.12.3
  passed `cmake --preset gcc-quality`, `cmake --build --preset gcc-quality`,
  `cmake --build --preset gcc-quality --target all_verify_interface_header_sets panda_format_checks`,
  `cmake --preset clang-cuda-analysis`, `cmake --build --preset clang-cuda-analysis`,
  `cmake --preset gcc-fast`, `cmake --build --preset gcc-fast --parallel` and
  `ctest --preset gcc-fast`: 517 of 525 cases passed and eight skipped (five
  CUDA/device-capability cases and three noncoherent-memory variants). CUDA
  analysis reported the same toolkit support warning as Windows.
- `python .agents/skills/panda-project-maintenance/scripts/check_docs.py` checked
  78 Markdown files and the task queue; `cmake --preset docs-checks` and its build
  validated 42 diagrams in 81 files; `cmake --preset gcc-docs` and its build
  generated the public API docs. The latest GCC coverage report measured 89.5%
  overall line coverage; all changed production lines in `Scene.cpp`, `Entity.cpp`
  and `Sphere.cpp` were exercised.
- A screenshot inspected earlier showed the sphere in the center of the eight-cube
  scene. This final verification rebuilt and tested the example but did not capture
  a new GPU image. Sanitizers and a RenderDoc capture were not run.
