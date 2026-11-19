# External graphics workflow

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Exercise the public graphics and optional import APIs together in one small
external C++ application. Extend the graphics-only consumer with the optional
import module. Use a fixed static GLB fixture with a metallic-roughness
material, color and data textures, and an explicit sampler.

Specifications: [Package and toolchain](../../design/package-and-toolchain.md#source-package-first),
[import adapter](../../design/contracts/scene.md#import-adapter).

## Acceptance

- Build and run the external CPM consumer on Windows and Linux with CUDA
  disabled.
- Import the GLB. Verify that an omitted metallic factor defaults to 1,
  the base-color map uses sRGB, the metallic-roughness map uses linear data,
  and the explicit sampler's filter and wrap settings reach the renderer.
- Render the model with a shadow using defaults, change presentation mode and
  MSAA while running, report the applied settings, and save an offscreen PNG
  of the same view. Record a capability skip if the surface offers only one
  presentation mode.
- Document setup and the essential public API calls so a new consumer can
  reproduce the workflow.

Learning: Trace a material from the GLB through texture upload to the rendered
image and explain where sampler settings apply.
