# Vulkan context and window lifecycle

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Create/select Vulkan 1.3 device with queried features, GLFW surface, maintenance1 swapchain, acquire/present/resize/minimize and bounded failure states.

Non-goal: No CUDA, scene or general render graph.

Interface: Context::create, checked FrameResult, feature diagnostics and swapchain maintenance1 lifecycle.

Paths: `../../../include/core/panda`, `src/vulkan/`, `src/window/`, `tests/gpu/` and directly affected documentation.
Specifications: [Window lifecycle](../../design/contracts/resources.md#frame-and-window-state), [rendering baseline](../../design/contracts/rendering.md#rendering-path).

## Acceptance

Run windowed smoke and error-path tests on Windows/Linux, including present fences,
acquired-image release and validation capture. Report unsupported maintenance1 as a
capability failure.

Learning: Trace acquire, submission completion and presentation lifetime separately.
