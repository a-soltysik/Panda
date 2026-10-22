# Vulkan context and window lifecycle

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Create/select Vulkan 1.3 device with queried features, GLFW surface, maintenance1 swapchain, acquire/present/resize/minimize and bounded failure states.

Non-goal: No CUDA, scene or general render graph.

Interface: `Context::create` creates a context without a surface;
`Context::createWithSurface` explicitly creates one with a borrowed window surface.
Both return the same checked context owner and use the shared `Result<T>` error
contract. The surface and native window outlive their context.

The shared error reports a recovery-oriented category, owned message, optional
native API status and source location. Native operation names are diagnostic
locations, not a public enum or required field.

Paths: `../../../include/core/panda`, `../../../src/core/vulkan/`, `../../../src/tools/gui/`, `../../../tests/cases/system/` and directly affected documentation.
Specifications: [Window lifecycle](../../design/contracts/resources.md#frame-and-window-state), [rendering baseline](../../design/contracts/rendering.md#rendering-path).

## Acceptance

Run windowed smoke and error-path tests on Windows/Linux, including present fences,
acquired-image release and validation capture. Report unsupported maintenance1 as a
capability failure. During physical-device selection, log and skip an adapter-local
inspection error so later devices can still qualify. Propagate surface-loss and
invalid-argument errors; if no device qualifies, return the first inspection error.

Learning: Trace acquire, submission completion and presentation lifetime separately.

## Current verification

- Windows: `cmake --build --preset msvc-fast --parallel` and
  `ctest --preset msvc-fast` passed. CTest registered 425 tests: 421 passed and
  four were skipped (the Linux-only buffered-close test and three unavailable
  noncoherent memory variants). Acquire timeout, not-ready, out-of-date and device
  loss paths leave the image index unused.
- WSL GCC: `cmake --build --preset gcc-fast --parallel` and
  `ctest --preset gcc-fast` passed. CTest registered 425 tests: 422 passed and
  three noncoherent memory variants were skipped because the device did not expose
  that memory type.
- Existing Windows Vulkan validation smoke presents, resizes, suspends/restores and
  destroys a moved Context on an NVIDIA GeForce RTX 4070 Ti. WSL system tests are
  Linux lifecycle evidence, not proof of hardware-GPU behavior.
- No new RenderDoc capture has been made. Previously observed capture-only
  `VUID-vkCmdDraw-None-09600` and
  `VUID-VkPresentInfoKHR-pImageIndices-01430` remain an integration limitation
  to recheck; the acquire-output change does not alter rendering barriers or draw
  commands.
- `python .agents/skills/panda-project-maintenance/scripts/check_docs.py` checked
  all 54 Markdown files and the task queue; `git diff --check` and Clang format
  validation passed.
