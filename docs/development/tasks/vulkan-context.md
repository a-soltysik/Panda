# Vulkan context and window lifecycle

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Create/select Vulkan 1.3 device with queried features, GLFW surface, maintenance1 swapchain,
acquire/present/resize/minimize and bounded failure states.

Non-goal: No CUDA, scene or general render graph.

Interface: `Engine::create(options)` and
`Engine::createWithSurface(surface, options)` both return the same concrete,
move-only `Engine` through `Result<Engine>`. The surface and native window are
borrowed and outlive a surface-enabled Engine. `Engine::getFeatures()` reports
window and performance availability; `getWindowRenderer()` and
`getPerformanceManager()` use `expect` when their optional services are absent.

The shared error reports a recovery-oriented category, owned message, optional
native API status and source location. Native operation names are diagnostic
locations, not a public enum or required field.

Paths: `../../../include/core/panda`, `../../../src/core/vulkan/`, `../../../src/tools/gui/`,
`../../../tests/cases/system/` and directly affected documentation.
Specifications: [Window lifecycle](../../design/contracts/resources.md#frame-and-window-state), [rendering baseline](../../design/contracts/rendering.md#rendering-path).

## Acceptance

Run windowed smoke and error-path tests on Windows/Linux, including present fences,
acquired-image release and validation capture. Report unsupported maintenance1 as a
capability failure. During physical-device selection, log and skip an adapter-local
inspection error so later devices can still qualify. Propagate surface-loss and
invalid-argument errors; if no device qualifies, return the first inspection error.

Learning: Trace acquire, submission completion and presentation lifetime separately.

## Current verification

- Windows: `cmake --preset msvc-fast`,
  `cmake --build --preset msvc-fast --parallel` and `ctest --preset msvc-fast`
  passed. CTest registered 645 tests: 641 passed and four were skipped (one
  Linux-only benchmark-output case and three unavailable noncoherent-memory
  variants). Window creation, rendering, capture, resize and headless rendering
  system tests passed.
- WSL GCC: `.agents/skills/panda-dual-platform-build/scripts/build-wsl.ps1 -Preset
  gcc-fast -RunTests -IncludeSystemTests` passed. CTest registered 645 tests: 637
  passed and eight were skipped (five CUDA interop cases unavailable in WSL and
  three unavailable noncoherent-memory variants). Window and headless system tests
  passed in the Linux environment.
- Commit 10 follow-up: the incremental `msvc-quality` build and its
  `all_verify_interface_header_sets`/`panda_format_checks` targets passed. After
  rebuilding the touched test targets, Windows CTest passed all six focused
  offscreen, handoff and window-rendering system tests. The synchronized WSL
  `gcc-fast` system-only run passed 43 of 50 tests; seven were skipped (four CUDA
  interop cases and three noncoherent-memory variants). This targeted run does not
  replace the full-suite results above.
- `python .agents/skills/panda-project-maintenance/scripts/check_docs.py` checked
  82 Markdown files and the task queue with no structural errors. Changed C++ files
  were formatted with Clang 22.1.8; `git diff --check` reported no whitespace
  errors. `cmake --preset docs-checks` and its build passed with 41 Mermaid
  diagrams checked; the WSL `gcc-docs` preset generated public API documentation
  without warnings.
- No new RenderDoc capture was made. System tests exercise rendering and capture,
  but this verification does not include visual inspection or a RenderDoc-based
  validation pass. Earlier capture-only validation messages remain unverified in
  this revision.
