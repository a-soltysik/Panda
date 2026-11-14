# Offscreen rendering and image readback

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Let applications render a `RenderView` without a window surface into an explicit
single-sample SDR color target with a depth attachment. Reuse the built-in renderer
and its material, lighting, alpha, shadow and output-encoding behavior. Provide
explicit, completion-tracked pixel readback to host memory so applications can
consume images and system tests can verify rendered pixels without capturing a
desktop window.

This is a GPU render target with optional CPU readback, not an implicit bitmap copy
on each render. File encoders, multiple render targets, a general render graph and
arbitrary application-provided Vulkan images are out of scope. Finalize public names,
pixel layout/color interpretation and failure behavior during implementation.

Paths: `../../../include/core/panda/`, `src/core/vulkan/renderer/`,
`src/core/vulkan/presentation/`, tests and directly affected documentation.
Specifications: [renderer](../../design/contracts/rendering.md#offscreen-output),
[resource lifetime and completion](../../design/contracts/resources.md#frame-and-window-state).

## Acceptance

- A Context without a window surface renders a supplied view to an offscreen target
  and returns completion information without presenting a frame.
- A window renderer can render once, present the view and copy that same output to
  a compatible offscreen target for capture.
- The result uses the same built-in mesh/material and alpha behavior as the window
  path, with a documented SDR color and depth format policy.
- Pixel readback is explicit, validates target ownership and completion, and has a
  documented extent, row layout, channel order and color-space interpretation.
- A GPU-backed system test renders overlapping Unlit Blend geometry, reads pixels,
  checks source-over output, and verifies the result is unchanged when the input draw
  order is reversed. It checks pixels away from primitive edges and uses a documented
  tolerance for format conversion.
- Tests cover invalid extents, the fixed RGBA8 sRGB output format and target lifetime
  across submitted work. No test accesses private renderer helpers.

## Verification

- Windows/MSVC, 2026-11-14: quality and fast builds, public-header/format targets,
  and CUDA analysis passed. `ctest --preset msvc-fast` passed 630 tests; 4 were
  skipped.
- WSL Ubuntu, 2026-11-14: quality and fast builds, public-header/format targets,
  and CUDA analysis passed. `ctest --preset gcc-fast` passed 630 tests; 8 CUDA and
  memory-type cases were skipped.
- Documentation checks passed: `check_docs.py` checked 82 Markdown files and the task
  queue; `docs-checks` validated 43 diagrams in 86 files; `gcc-docs` generated the
  public API pages. `git diff --check` passed.

Learning: Explain why rendering to a GPU target does not itself copy pixels to the
CPU, and how the completion point protects the target and its readback.
