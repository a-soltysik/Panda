# Environment lighting and skybox

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Add opt-in image-based lighting (IBL) to built-in Lit materials for v1. Accept
two linear HDR cubemaps per scene: one stores diffuse irradiance; the other
stores specular radiance filtered by roughness across mip levels. This task
includes cubemap upload and sampling, plus an internal CPU GGX prefilter for
specular cubemap mip levels. Applications provide the sharp source cubemap and
prepare diffuse irradiance separately. `Scene` retains environment handles and
copies them into each `RenderView` snapshot. Direct renderer users can still set
the same fields on `RenderView`.

Accept a separate optional application-supplied linear HDR cubemap as the visible
skybox. Its unfiltered source radiance represents the background. `Scene` retains
this skybox independently from IBL; both can be enabled or cleared separately.
Direct renderer users can set the skybox on `RenderView`. Reuse the cubemap upload
and resource-lifetime rules for the background.

When an environment is supplied, its diffuse contribution replaces the simple
ambient term and its reflections contribute alongside direct lighting and
shadows. Views without an environment retain the current lighting behavior.

Specifications: [Rendering path](../../design/contracts/rendering.md#rendering-path),
[built-in materials](../../design/contracts/rendering.md#built-in-materials).

## Acceptance

- Use one fixed environment fixture to show diffuse illumination in the
  absence of direct lights. Verify that a smooth metal reflects a bright
  feature more sharply than a rough metal and check diffuse response on a
  dielectric.
- Reject incomplete or incompatible cubemaps before submission. Retain the
  environment resources while a submitted frame uses them.
- Verify the same environment response in window and offscreen output.
- Verify that a scene without an environment still uses the existing ambient
  and direct-light behavior.
- Verify skybox-only, IBL-only, combined and solid-background views. Check
  that camera rotation changes the visible sky, camera translation leaves it
  fixed, and foreground geometry occludes it.
- Verify `Scene::createRenderView()` copies the Scene environment and skybox into
  snapshots, and later Scene changes do not alter an existing snapshot.
- Compare window and offscreen output using the same view and skybox. Verify
  color encoding, one tone-mapping operation, and skybox handle validation
  and retention through frame completion.
- Verify the internal specular filter preserves mip zero, creates a full roughness
  chain in RGBA16F, samples across face edges, and rejects invalid input without
  modifying caller data.
- Verify `TextureMipPolicy::GenerateSpecularEnvironment` accepts six standard
  mip-0 face writes, generates every roughness mip with the configured sample
  count, rejects duplicate or incomplete faces, and permits retry after an
  incomplete submission.

Learning: Compare how the same environment affects a smooth metal and a rough
dielectric. [LearnOpenGL's cubemap chapter](https://learnopengl.com/Advanced-OpenGL/Cubemaps)
explains skybox sampling; [Filament's IBL section](https://google.github.io/filament/Filament.md)
explains environment lighting and prefiltered reflections.

## Current implementation

`Texture` supports square RGBA16F cubemaps with Vulkan cube views. Upload batches
accept one complete face per layer and mip, and reject duplicate or incomplete
subresources before submission. `TextureMipPolicy::GenerateSpecularEnvironment`
accepts six base-level face writes and uses
`TextureDesc::specularEnvironmentSamplesPerTexel` to run the internal GGX prefilter
when the set is complete; ordinary 2D `TextureMipPolicy::Generate` retains box
downsampling for RGBA8/BGRA8 UNORM and sRGB formats, and rejects unsupported
formats at texture creation. `Scene::setEnvironment()` and `Scene::setSkybox()` retain independent
scene-wide resource handles, which `SceneRenderViewBuilder` copies into each
`RenderView`. Direct `RenderView` callers can still supply them manually. Render
submission validates uploaded, same-Engine diffuse, specular and skybox maps.
Built-in Lit pipelines
bind them through frame descriptors; diffuse irradiance replaces simple ambient,
and the specular map uses reflection direction and roughness-selected mips.
Frame slots retain both maps until their submitted work completes.
`RenderView::skybox` independently validates and retains an uploaded same-Engine
RGBA16F cubemap. Its fullscreen triangle samples source mip 0 using camera
orientation, ignores camera translation, and draws before geometry through both
window and offscreen renderers. It shares the built-in ACES tone curve and output
encoding. Offscreen and window capture tests cover environment response, skybox
rotation, translation, geometry occlusion, output encoding, compatibility,
lifetime, and unchanged behavior without an environment. Scene mesh instances copy
built-in material values into each render snapshot. `scene_demo` uses the sharpest
specular mip as its visible background and smooth metal materials on the neutral
sphere and gray ground to show the colored reflections. `scene_demo` uploads six
procedural HDR base faces through the ordinary region-writing API and lets the
texture policy generate the specular roughness mips. The diffuse map has one mip
and remains application-prepared.

## Verification

- Windows: MSVC 19.51.36260.0, CMake 4.4.2 and ccache 4.13.6 (`disable=false`).
  `msvc-quality`, interface-header verification, `panda_format_checks`,
  `clang-cuda-analysis-windows`, and `msvc-fast` all built successfully.
  `ctest --preset msvc-fast` passed all 712 tests with 5 expected skips: 2
  output-device failure cases and 3 non-coherent-memory cases.
- WSL Ubuntu: GCC 16.0.1 experimental, CMake 4.2.3, shaderc 2026.1-1,
  SPIRV-Tools 2026.1 and ccache 4.12.3 (`disable=false`). `gcc-quality`, interface-
  header verification, `panda_format_checks`, `clang-cuda-analysis`, and
  `gcc-fast` built successfully. `ctest --preset gcc-fast` passed all 712 tests
  with 8 expected skips: 5 CUDA interop cases and 3 non-coherent-memory cases.
- The documentation checker, Mermaid checks and Linux API documentation build
  passed. Codecov reported 81.85% patch coverage before the latest test additions;
  coverage was not rerun locally, and the updated result is left to the PR check.
- The offscreen renderer was exercised by the system tests. `scene_demo` was not
  launched for visual inspection. `git diff --check` passed.
