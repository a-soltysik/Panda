# Vulkan window example

`simple_scene` opens a window, creates a unit-cube mesh and uploads its vertices
and indices, then displays a gray background. The mesh is retained throughout the loop;
rendering geometry is not implemented yet, so the cube is not visible. Requires a
display, Vulkan 1.3 with swapchain maintenance1, and the Khronos validation layer.
CUDA is not needed.

The upload waits up to ten seconds before entering the loop. The loop does not
wait after every frame or track a shutdown completion point; Context drains
submitted GPU work and presentation during RAII destruction.

The example adds a `StreamSink` to `std::clog` before starting Panda. The logger
starts with no sinks, so applications choose where engine logs are written.

Run from the repository root.

Essential operations use `panda::expect`: a failed operation aborts with context,
including in Release builds. Normal shutdown uses RAII; abort does not unwind
local objects. See [error handling](../error_handling/README.md) for recovery,
error propagation and detailed diagnostics.

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
