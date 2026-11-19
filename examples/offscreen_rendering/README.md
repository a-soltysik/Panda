# Headless offscreen rendering example

`offscreen_rendering` creates a headless Engine with `Engine::create()`, uploads a cube,
and renders four animated frames into one RGBA8 sRGB `OffscreenTarget`. Each frame
uses `OffscreenRenderer::render()` followed by an explicit `readPixels()` call.
This demonstrates GPU rendering and readback without a display or window-system
surface.

It also generates a 2048×2048 stripe texture in memory, maps it onto a plane that
recedes into the distance, and writes `texture_filtering_comparison.png`. The 2×2
image compares Nearest, Bilinear, Trilinear and Anisotropic 8x with the same camera
and 4x MSAA request. The program reports the effective sample count and anisotropy;
unsupported anisotropy is capped or falls back according to the Engine contract.
The panel order is Nearest (top left), Bilinear (top right), Trilinear (bottom left),
and Anisotropic 8x (bottom right). MSAA is held constant so the comparison isolates
texture filtering and texture aliasing. Fine stripes fade toward gray as the mip
levels average them at a distance; anisotropy retains more detail on the oblique
surface. MSAA smooths geometric edges and does not restore that texture detail. No
image assets are required.

For each frame, the program writes PNG, raw RGBA, BMP and JPEG files in its
working directory (`offscreen_frame_0.*` through `offscreen_frame_3.*`). The raw
file contains tightly packed, top-to-bottom RGBA bytes without extent metadata.

Run from the repository root. The example requires the optional `Panda::Tools`
component for image file export.

Windows (MSVC):

```powershell
cmake --preset msvc-development
cmake --build --preset msvc-development --target offscreen_rendering
./build-msvc-development/examples/offscreen_rendering/offscreen_rendering.exe
```

Linux (GCC):

```sh
cmake --preset gcc-development
cmake --build --preset gcc-development --target offscreen_rendering
./build-gcc-development-Linux/examples/offscreen_rendering/offscreen_rendering
```
