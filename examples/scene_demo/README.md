# Scene hierarchy example

`scene_demo` uses `Panda::Scene` to render nine colored cubes and a ground plane.
The cubes share one uploaded mesh and belong to one group. The application updates
each cube's local height with a radial sine wave: its phase depends on the cube's
distance from the group's origin. Local heights range from 0 to 1.6 m; each cube's
local horizontal position, rotation and scale remain fixed. Independently, the whole
group rotates around Y at 0.3 radians per second. `animateWave()` demonstrates
per-object animation and `animateGroup()` demonstrates parent animation; their
transforms compose automatically into the world matrices used by the renderer.

The camera and directional light are named scene entities. Their local -Z axes
define the viewing and lighting directions. Each frame supplies the current
framebuffer aspect ratio to `Scene::createRenderView()`, then passes the returned
snapshot to `Context::render()`. The application owns the loop and uploads; scene
objects and snapshots retain mesh handles. Context outlives all of them.

Requires `PANDA_BUILD_SCENE=ON`, Tools, a display, Vulkan 1.3 with swapchain
maintenance1, and the Khronos validation layer. The development presets enable the
scene module. The renderer currently supports one directional light and its shadow
pass. Additional visible directional lights report `Unsupported`.

For the shorter direct-renderer path, see [simple_scene](../simple_scene/README.md).

Windows:

```powershell
cmake --preset msvc-development
cmake --build --preset msvc-development --target scene_demo
./build-msvc-development/examples/scene_demo/scene_demo.exe
```

Linux:

```sh
cmake --preset gcc-development
cmake --build --preset gcc-development --target scene_demo
./build-gcc-development-Linux/examples/scene_demo/scene_demo
```
