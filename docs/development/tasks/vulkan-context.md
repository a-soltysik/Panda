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

- Windows MSVC: `cmake --build build-msvc-development --target simple_scene -j 2` succeeds and links the example.
- Windows MSVC with `VK_INSTANCE_LAYERS`, `VK_LAYER_DEBUG_ACTION` and `VK_LAYER_LOG_FILENAME` removed: the six `ContextTest` cases and the real-device `VulkanContextSystem.SelectsAndOwnsRequiredDevice` test pass (7/7). The system test requests validation and completes without a skip.
- WSL GCC: `panda_unit_context` builds with warnings as errors and all six `ContextTest` cases pass. `simple_scene` builds in `build-gcc-development-Linux`. In `build-engine-linux`, `VulkanContextSystem.SelectsAndOwnsRequiredDevice` passes with the validation-layer environment variables removed.
- WSL GCC coverage: all 65 unit and integration tests pass. From the repository root, `gcovr --root . --gcov-executable gcov-16 --filter 'src/.*' --html-details build-gcc-coverage-fresh-Linux/coverage/index.html --cobertura build-gcc-coverage-fresh-Linux/coverage/coverage.xml --print-summary build-gcc-coverage-fresh-Linux` reports lines 74.6% (1098/1471), functions 85.2% (161/189), and branches 48.3% (745/1543). Reports stay under the build directory.
- GCC sanitizers: the five `PresentationTest` cases pass under ASan/UBSan, covering acquire retirement after recording failure, image release after present failure, and recreation retry after swapchain retirement.
- Windows MSVC and WSL GCC: all five `PresentationTest` cases pass after aligning the acquire-image barrier source stage with the semaphore wait stage. `simple_scene` also rebuilds with MSVC, and `clang-format --dry-run --Werror` passes for the changed source and test.
- A standalone Windows run of the current `simple_scene` with Context-owned validation produced an empty `.cache/vulkan-validation-after-sync-fix.log` (0 bytes), so it did not reproduce `VUID-vkCmdDraw-None-09600` outside RenderDoc.
- The fresh `.cache/Capture-after-sync-fix.rdc` records both transitions for the same swapchain image: the acquire wait and transition into `COLOR_ATTACHMENT_OPTIMAL` use `COLOR_ATTACHMENT_OUTPUT`, then rendering transitions back to `PRESENT_SRC_KHR`. The recorded command buffer contains no `vkCmdDraw`.
- The validation log produced with that RenderDoc capture still reports `VUID-vkCmdDraw-None-09600` and `VUID-VkPresentInfoKHR-pImageIndices-01430`, with `GENERAL` as the reported current layout. Neither message appears in the standalone run. This isolates the report to the RenderDoc capture run and points to an interaction between RenderDoc and validation; it does not reproduce as a Panda layout error outside capture.
- `clang-format --dry-run --Werror` and `python .agents/skills/panda-project-maintenance/scripts/check_docs.py` pass.
