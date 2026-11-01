# Vulkan window example

`simple_scene` opens a window, creates a unit-cube mesh and uploads its vertices
and indices, then displays a gray background. The mesh is retained throughout the loop;
rendering geometry is not implemented yet, so the cube is not visible. Requires a
display, Vulkan 1.3 with swapchain maintenance1, and the Khronos validation layer.
CUDA is not needed.

The loop keeps the last committed frame completion point and explicitly waits
for its GPU work at normal shutdown, with a ten-second timeout. It does not wait
after every frame. This point does not track display completion; Context still
drains presentation during destruction.

The example adds a `StreamSink` to `std::clog` before starting Panda. The logger
starts with no sinks, so applications choose where engine logs are written.

Run from the repository root.

Windows (MSVC):

```powershell
cmake --preset msvc-development
cmake --build --preset msvc-development --target simple_scene
./build-msvc-development/examples/simple_scene/simple_scene.exe
```

Linux (GCC):

```sh
cmake --preset gcc-development
cmake --build --preset gcc-development --target simple_scene
./build-gcc-development-Linux/examples/simple_scene/simple_scene
```
