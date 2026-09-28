# Empty-window example

`simple_scene` uses `Panda::Tools` to create a GLFW window without an OpenGL
context. It waits for input and closes when the user closes the window. The
window owner releases GLFW resources on exit, including error and exception
paths. The example does not render a scene yet.

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

An X11 display (or XWayland) is needed on Linux. An empty Wayland surface is
not mapped until it presents a buffer, so this example selects X11. Core
applications that do not need a window can omit `PANDA_BUILD_TOOLS`.

The development presets run Panda's analysis and formatting checks; the
[root README](../../README.md#build-and-run) lists how to disable them when
those tools are unavailable. CUDA is not needed for this example.
