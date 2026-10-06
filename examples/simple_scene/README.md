# Vulkan window example

`simple_scene` creates a GLFW window through `Panda::Tools`, then passes that
window to `panda::Context::createWithSurface` to create a Vulkan 1.3 context. It clears and presents
frames, processes window events and waits for input while the framebuffer has
zero size. The context is destroyed before the window, including error and
exception paths. This is a diagnostic frame; scene geometry is not available yet.

Errors share `panda::Error`: the example logs the message and source location, and
includes native status when one exists. Applications can simply display the message
or inspect the recovery category and native status for a response. Context creation
reports an unsupported capability when the device lacks swapchain maintenance1.
A visible display and a Vulkan-capable device are needed.

From the repository root, build and run on Windows with MSVC:

```powershell
cmake --preset msvc-development -DPANDA_BUILD_TOOLS=ON -DPANDA_BUILD_EXAMPLES=ON
cmake --build --preset msvc-development
./build-msvc-development/examples/simple_scene/simple_scene.exe
```

On Linux with GCC:

```sh
cmake --preset gcc-development -DPANDA_BUILD_TOOLS=ON -DPANDA_BUILD_EXAMPLES=ON
cmake --build --preset gcc-development
./build-gcc-development-Linux/examples/simple_scene/simple_scene
```

GLFW selects the available window platform on Linux. Core applications that do
not need a window can omit `PANDA_BUILD_TOOLS`.

The development presets enable compiler warnings; quality presets add static
analysis and formatting. CUDA is not needed for this example.
