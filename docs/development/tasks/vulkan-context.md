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

- Windows MSVC 14.51 with Vulkan SDK 1.4.341.1: `cmake --build build-msvc-tests -j 2` succeeds; `ctest --test-dir build-msvc-tests --output-on-failure -LE system` passes all 91 unit/integration tests. The unit boundary keeps Context, device selection and swapchain creation real while substituting native Vulkan calls; 22 added cases cover complete owners, staged failures, destruction ordering, undefined failed enumeration outputs and incomplete-result retry.
- Windows real-device smoke: `ctest --test-dir build-msvc-tests --output-on-failure -V -R 'system.*ContextSystem'` passes all three context/window tests without skips on NVIDIA GeForce RTX 4070 Ti. Remove `VK_INSTANCE_LAYERS`, `VK_LAYER_DEBUG_ACTION` and `VK_LAYER_LOG_FILENAME`; set `VK_LOADER_LAYERS_DISABLE=~implicit~` for this test process to isolate installed overlays while Context requests Khronos validation. Logs contain no Vulkan VUID or validation error. The window test presents, resizes, suspends/restores and destroys a moved Context.
- WSL GCC 16.0.1 with Vulkan headers 1.4.341: `scripts/build-wsl.ps1 -Preset gcc-tests -IncludeSystemTests -Jobs 2` passes all 95 tests without skips. Native Vulkan tests use llvmpipe (LLVM 21.1.8), a software implementation; this is Linux lifecycle evidence, not Linux hardware-GPU evidence. Their logs contain no Vulkan VUID or validation error.
- WSL ASan/UBSan: `scripts/build-wsl.ps1 -Preset gcc-sanitizers -RunTests -Jobs 2` passes all 91 non-system tests. Failure cases prevent dispatcher creation for a failed instance, never publish or destroy nonnull failed native outputs, release previously created resources, and preserve acquire/present/recreation recovery.
- WSL CPU coverage: `scripts/build-wsl.ps1 -Preset gcc-coverage -RunTests -Jobs 2` passes all 91 non-system tests. gcovr 7.2 with `--gcov-executable gcov-16 --filter 'src/.*'` and the managed mirror as `--root` reports lines 88.0% (1286/1462), functions 98.5% (191/194), and branches 58.6% (920/1569). HTML, Cobertura XML and JSON reports remain in ignored cache/build directories. Context.cpp has 93.7% line coverage and SwapchainGeneration.cpp 99.1%. Counts come from reset profiles and a fresh 91-test run. The filter matches CI and excludes SDK/dependency code; native GPU tests are not part of this measurement.
- WSL quality with clang-tidy 22.1.2 and cppcheck 2.19.0: `scripts/build-wsl.ps1 -Preset gcc-quality -Target panda_core,panda_unit_context,panda_unit_presentation,panda_unit_core,panda_system_windowed_context,all_verify_interface_header_sets -Jobs 2` succeeds, including isolated public-header compilation and formatting checks. `misc-use-internal-linkage` remains enabled; the private nested instance-services type needs no suppression.
- Examples/build modes: `cmake --build build-msvc-development --target simple_scene -j 2`, `cmake --build build-msvc-unity --target simple_scene -j 2` and `scripts/build-wsl.ps1 -Preset gcc-unity -Target simple_scene -Jobs 2` succeed. Legacy sources/assets remain outside these targets.
- `python .agents/skills/panda-project-maintenance/scripts/check_docs.py` checks all 44 Markdown files and the task queue without structural errors.
- No new RenderDoc capture has been made. Previously observed capture-only `VUID-vkCmdDraw-None-09600` and `VUID-VkPresentInfoKHR-pImageIndices-01430` remain an integration limitation to recheck; the current standalone smoke runs do not reproduce them. The RAII ownership change does not alter the clear-frame barriers or introduce drawing.
