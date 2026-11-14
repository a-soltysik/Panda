# Headless offscreen rendering example

`offscreen_rendering` creates a Vulkan Engine without a window, uploads a cube,
and renders four animated frames into one RGBA8 sRGB `OffscreenTarget`. Each frame
uses `OffscreenRenderer::render()` followed by an explicit `readPixels()` call.
This demonstrates GPU rendering and readback without a display or window-system
surface.

The program writes `offscreen_frame_0.rgba` through `offscreen_frame_3.rgba` in its
working directory. Each file contains tightly packed, top-to-bottom RGBA bytes;
RGB is sRGB encoded and alpha is linear UNORM. The files are raw pixel data, not
PNG or JPEG images; encoding is planned for the optional `Panda::Tools` package.

Run from the repository root.

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
