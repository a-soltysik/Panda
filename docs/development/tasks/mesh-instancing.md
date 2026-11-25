# Mesh instancing

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Render repeated ordinary meshes in batches with per-instance transforms and
tint. The direct renderer accepts explicit instance lists, and `SceneRenderViewBuilder`
groups compatible scene objects when it creates a snapshot. Preserve separate
scene entities and support built-in materials in camera and shadow passes.

Specifications: [Rewrite scope](../../design/scope.md#requirements),
[instancing and billboards](../../design/contracts/rendering.md#instancing-and-billboards).

## Acceptance

- Verify that compatible copies of one mesh use an instanced draw in each
  applicable pass and that draw counts scale with batches rather than copies,
  including compatible objects collected from `Scene`.
- Compare each instance's camera placement, tint and shadow placement with
  the equivalent individual draws.
- Keep blend ordering and custom mesh shader behavior compatible with their
  existing single-draw paths.

## Verification

- Windows: MSVC 19.51.36260.0, Vulkan SDK 1.4.341.1 and CUDA 12.9. The
  `msvc-quality` build, interface-header and format targets, Windows CUDA
  analysis, and `msvc-fast` build passed. `ctest --preset msvc-fast` passed all
  801 tests; five were skipped (two expected full-output cases and three
  non-coherent-memory cases).
- WSL Ubuntu: GCC 16.0.1, CMake 4.2.3 and CUDA 12.9. The `gcc-quality` build,
  interface-header and format targets, CUDA analysis, and `gcc-fast` build
  passed. `ctest --preset gcc-fast` passed all 801 tests; eight CUDA, external
  memory and non-coherent-memory cases were skipped for local capability limits.
- `python .agents/skills/panda-project-maintenance/scripts/check_docs.py`
  checked 92 Markdown files and the task queue. `docs-checks` accepted all 45
  Mermaid diagrams in 99 files. The `gcc-docs` build generated the public API
  documentation.
- CUDA analysis reported that toolkit 12.9 is newer than its latest partially
  supported version; the configured analysis preset permits this warning. GPU
  tests ran on Windows, but no visual capture or performance benchmark was made.

Learning: Explain which changes require another batch and which fit in the
per-instance data.
