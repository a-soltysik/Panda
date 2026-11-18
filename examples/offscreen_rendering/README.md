# Headless offscreen rendering example

`offscreen_rendering` creates a headless Engine with `Engine::create()`, uploads a cube,
and renders four animated frames into one RGBA8 sRGB `OffscreenTarget`. Each frame
uses `OffscreenRenderer::render()` followed by an explicit `readPixels()` call.
This demonstrates GPU rendering and readback without a display or window-system
surface.

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
