# Vulkan window example

`simple_scene` opens a window, creates a unit cube and unit plane with the standard
geometry helper, then creates a compact position-and-color triangle with
`Context::createMesh(MeshDesc)`. One upload batch initializes all three meshes,
the cube's optional 2x2 sRGB base-color and emissive maps, and neutral 1x1 linear
metallic-roughness map. A second draw of the same cube uses a neutral gray material,
a 4x4 linear normal map with alternating tangent-space directions, and a linear
occlusion map with a dark center patch. Its striped lighting shows the normal map;
the patch reduces only ambient light and does not alter direct light or the cube's
shadow. Neither map changes cube geometry. The emissive map adds orange emission to part of the textured cube;
it does not illuminate other objects or produce a bloom halo. Both cubes use
standard UVs; the custom triangle has no UVs and renders without a map. The view
renders the cubes and ground with the built-in directional light,
depth-map shadows and lit material, plus the triangle with an unlit material.
The custom triangle shows how a mesh can use its own vertex stride and attribute
offsets instead of the standard 64-byte geometry layout; its vertex data and draw
setup are in [main.cpp](main.cpp).
`panda::createPerspectiveCamera` builds the camera values from Core, and the
application supplies the mesh draw; no scene or ECS library is involved.
Requires a display, Vulkan 1.3 with swapchain maintenance1, and the Khronos validation
layer. CUDA is not needed.

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
