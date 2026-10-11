# Vulkan window example

`simple_scene` opens a window and displays a gray background. Scene geometry is
not implemented yet. Requires a display, Vulkan 1.3 with swapchain maintenance1,
and the Khronos validation layer. CUDA is not needed.

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
