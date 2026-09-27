# GLFW window wrapper

Order, dependencies and status: [plan](../PLAN.md).
Specifications: [window wrapper](../../design/package-and-toolchain.md#window-wrapper),
[errors](../../design/contracts/resources.md#errors) and [quality](../quality.md).

## Result

A checked, move-only `panda::tools::Window` with recoverable creation, query and
event errors, shared GLFW lifetime and direct logging through common diagnostics.
The optional `simple_scene` example owns a local Window and a plain event loop;
no engine runtime class is required.
This explicitly requested wrapper work does not authorize Vulkan context,
swapchain, rendering or the rest of the GPU resources phase.

Paths: `../../../include/tools/panda/tools/gui/Window.hpp`,
`../../../src/tools/gui/Window.cpp`, `../../../src/tools/gui/GlfwSession.hpp`,
`../../../src/tools/gui/GlfwSession.cpp`, `../../../examples/simple_scene/main.cpp`,
Tools/example CMake and window tests.

## Acceptance

- Fallible construction uses a factory returning `std::expected` with owned
  operation context and native status. Failed creation leaves no window/session.
- Two windows share initialization; destroying one does not terminate the other.
  Moving transfers ownership without double destruction or lost identity.
- Queries read current dimensions and distinguish backend errors from minimization.
  Event failures are returned; moved-from and wrong-thread use are checked in Release.
- Ordinary logs are direct; exception containment is confined to the C callback.
  GLFW headers remain private; Window and its session belong to the Tools module archive.
- `PANDA_BUILD_EXAMPLES` adds `simple_scene` and requires Tools, selected through
  `PANDA_BUILD_TOOLS` or enabled automatically by `PANDA_BUILD_TESTS`. Its application
  loop waits for events, handles errors and releases its window through RAII.
  Builds with tests and Tools disabled do not discover or link GLFW.
- Report real quality-build/CPU checks, real GLFW smoke evidence and unavailable
  native-display/platform checks without treating them as passes.

## Evidence and limits

Windows GCC 16.1.0 and Linux GCC 16.0.1 non-unity Debug builds compiled Tools and
`simple_scene`. The Windows core-only build contains no GLFW, and configuring
examples with both tests and Tools disabled is rejected. Maintainer-provided Windows runtime logs show
window creation and loop completion; window appearance has not been reviewed.

Unit tests inject initialization, creation, size,
framebuffer and event failures and check retry, callback restoration, multi-window
ownership and movement. GoogleTest death tests in `tests/unit/tools/Window.cpp`
cover moved-from use, wrong-thread use and waiting without a live window.
All 11 window unit cases and the real GLFW 3.4 null-platform integration passed on
Windows GCC 16.1.0, Linux GCC 16.0.1 and Windows MSVC 19.51.36260. The production
window implementation belongs to the static Tools module; the thin GLFW forwarding
stub is a shared object target. Unit tests link `Panda::Tools` and inject the stub
objects directly, without recompiling production sources. Real GLFW remains a
normal archive dependency; the stub supplies the symbols used by those tests.
`WindowLifecycle.cpp` tests integration; `ApplicationStartup.cpp` is a separate
system scenario requiring an available native display and was not executed.
The empty-window example selects X11 on Linux; a Wayland compositor does not map
its bufferless surface. The Window library retains automatic platform selection
for future rendering clients. On WSLg, the Clang-built example was reported by X11 as mapped (`IsViewable`),
processed a `WM_DELETE_WINDOW` event and exited successfully with both loop logs.
Its logs display project-relative paths. This verifies native mapping and teardown;
visual appearance, Release and GPU behavior remain unverified.

Learning: each window owns its native handle and shares only the GLFW session,
managed by the standalone internal `panda::tools::GlfwSession` class.
The final session release terminates GLFW after the last native window is destroyed.
