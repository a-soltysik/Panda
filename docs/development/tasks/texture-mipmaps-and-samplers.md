# Texture mipmaps and samplers

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Upload supplied mip chains for sampled 2D textures and support generating the
chain from a base level when requested. Keep single-level textures usable.
Built-in material maps support nearest and linear filtering, nearest and
linear mip selection, and repeat, mirrored repeat and clamp-to-edge addressing.
Use trilinear filtering by default on mipmapped material textures.

Expose runtime anisotropy off, 2x, 4x, 8x and 16x for trilinear material
samplers. Request 8x by default, cap it at the device limit, and use trilinear
without anisotropy when unavailable. Preserve explicit nearest and single-level
sampling policies.

Specifications: [Anti-aliasing and sampling](../../design/contracts/rendering.md#anti-aliasing-and-sampling),
[import adapter](../../design/contracts/scene.md#import-adapter).

## Acceptance

- Verify both supplied and generated mip chains. Generation averages sRGB color
  in linear light and treats linear data as linear. Verify both with known
  texels and check that minification selects the expected mip.
- Verify minification filtering and wrap modes with a textured surface
  receding into the distance. Retain texture and sampler resources through
  completion when settings change.
- Report effective anisotropy to the application and benchmark. Test caps and
  the fallback when anisotropy is unavailable.

Learning: Compare mipmapped trilinear filtering with anisotropy on a surface
viewed at a grazing angle.
