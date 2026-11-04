# Scene hierarchy example

`scene_demo` uses `Panda::Scene` to render eight colored cubes, a sphere and a ground
plane. The cubes and sphere share a parent group; the sphere replaces the center cube
and uses a separate uploaded mesh. The application updates each object's local
height with a radial sine wave: its phase depends on its distance from the group's
origin. Local heights range from 0 to 1.6 m; each object's local horizontal position,
rotation and scale remain fixed. Independently, the whole group rotates around Y at
0.3 radians per second. `animateWave()` demonstrates per-object animation and
`animateGroup()` demonstrates parent animation; their transforms compose automatically
into the world matrices used by the renderer.

The scene starts empty. The application explicitly creates its camera and
directional-light entities, attaches their components and selects the camera. It
also creates and uploads the cube, ground and configurable sphere meshes, then
associates them with scene objects. The sphere uses 48 longitude segments and 24
latitude rings. Each frame supplies the current framebuffer aspect ratio to
`Scene::createRenderView()` and passes the returned snapshot to
`Context::render()`. The application owns the loop and uploads; scene objects and
snapshots retain mesh handles. Context outlives all of them.

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
