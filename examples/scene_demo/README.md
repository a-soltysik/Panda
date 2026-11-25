# Scene hierarchy example

`scene_demo` uses `Panda::Scene` to render seven colored cubes, a sphere, an
upright vertex-colored triangle and a ground plane. The sphere occupies the center
grid slot, and the triangle replaces one corner cube. The triangle and sphere use
separate meshes; the seven cubes and sphere share one animated parent group. The
triangle remains outside that rotating group so it keeps its orientation as the grid
turns. The application updates each object's local height with a radial sine wave
whose phase depends on its distance from the group's origin. Local heights range
from 0 to 1.6 m. Horizontal position, rotation and scale remain fixed. Independently,
the group rotates around Y at 0.3 radians per second. `animateWave()` demonstrates
per-object animation and `animateGroup()` demonstrates parent animation; their
transforms compose automatically into the world matrices used by the renderer.
The seven cubes share one mesh and material, so each snapshot groups them into one draw with seven instances; their
different transforms and tints remain per-instance.

The scene starts empty. The application explicitly creates its camera and
directional, point and spot light entities, attaches their components and selects the camera. It
also creates and uploads the cube, ground, configurable sphere and triangle meshes,
then associates them with scene objects. The sphere uses 48 longitude segments and 24
latitude rings. Each frame supplies the current framebuffer aspect ratio to
`Scene::createRenderView()` and passes the returned snapshot to
`WindowRenderer::render()`. The application owns the loop and uploads; scene objects and
snapshots retain mesh and environment handles, so their Engine stays alive until those
handles are released.
The demo builds a small procedural linear-HDR source cubemap, creates diffuse irradiance data,
and writes its six base faces through `UploadBatch::write()` with `TextureUploadRegion` for a cubemap
created with `TextureMipPolicy::GenerateSpecularEnvironment`. The upload generates and stages the
GGX specular roughness mips using `TextureDesc::specularEnvironmentSamplesPerTexel`.
It stores both maps on `Scene`, which copies them into each returned render view. The sharpest
specular mip also serves as the visible skybox. The center sphere is a neutral, smooth metal, and
the gray ground is moderately rough metal with softer colored reflections; both make the
environment visible on surfaces. The moving Lit objects receive image-based light alongside the
direct lights, while the skybox is drawn behind them. The example uses a small fixed sample count
and resolution for startup; applications can precompute larger maps with more samples for content.
`Engine::createWithSurface` returns the same move-only `Engine` type used by the
headless path. Common service access stays on Engine, and this surface-enabled
Engine also provides `getWindowRenderer()`.

Requires `PANDA_BUILD_SCENE=ON`, Tools, a display, Vulkan 1.3 with swapchain
maintenance1, and the Khronos validation layer. The development presets enable the
scene module. The directional key, point and spot lights opt into shadow maps; the
lower-intensity directional fill remains unshadowed so it does not wash out the
shadows. The ground uses a darker tint to keep its illumination below display white.
Point shadows use six atlas tiles, while the spot light uses one tile sized from its
cone and finite range.

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
