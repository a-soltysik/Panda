# Texture mipmaps and samplers

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Upload supplied mip chains for sampled 2D textures and support generating the
chain from a base level when requested. Keep single-level textures usable.
Built-in material maps use one Engine-wide filtering preset: nearest, bilinear,
trilinear, or anisotropic 2x, 4x, 8x or 16x. Anisotropic presets use
trilinear sampling; request 8x by default, cap it at the device limit, and use
trilinear when anisotropy is unavailable. Keep per-texture min/mag/mipmap filter
combinations out of the public API. `TextureDesc` keeps mip policy and one
address mode applied to both UV axes.

Specifications: [Anti-aliasing and sampling](../../design/contracts/rendering.md#anti-aliasing-and-sampling),
[import adapter](../../design/contracts/scene.md#import-adapter).

## Acceptance

- Verify both supplied and generated mip chains. Generation averages sRGB color
  in linear light and treats linear data as linear. Verify both with known
  texels and check that minification selects the expected mip.
- Verify minification filtering and wrap modes with a textured surface
  receding into the distance. Retain texture and sampler resources through
  completion when the Engine preset changes between frames.
- Report effective anisotropy to the application and benchmark. Test caps and
  the fallback when anisotropy is unavailable.

## Current implementation

The public API exposes one Engine-wide `TextureFilteringPreset`, configured in
`EngineOptions` and changeable at runtime; textures keep mip policy and one UV
address mode. Supplied and CPU-generated chains are implemented, with minified
offscreen rendering selecting and sampling the expected mip levels. The
offscreen example generates a large stripe texture and writes a visual comparison
for four filtering presets.

Learning: Run the [offscreen rendering example](../../../examples/offscreen_rendering/README.md)
and compare mipmapped trilinear filtering with anisotropy on its receding stripe
surface. It writes a 2-by-2 PNG with Nearest, Bilinear, Trilinear and Anisotropic 8x
panels, keeping the sample count fixed across them.

## Verification

- Windows `msvc-fast` build and full CTest: 693 tests, 0 failures, 5 skips.
- WSL `gcc-fast` build and full CTest: 693 tests, 0 failures, 8
  hardware/environment skips. The CI-equivalent `gcc-coverage-all` run under Xvfb
  and lavapipe passed 674 tests with 4 environment skips; the source coverage
  report records 95.1% line and 84.2% branch coverage.
- Windows and WSL quality builds, public-header checks, format checks and Clang
  CUDA host/device analysis passed. `check_docs.py` checked 89 Markdown files;
  `docs-checks` validated 42 diagrams across 93 files; Linux API documentation
  generated successfully.
- The offscreen example generated the comparison on an NVIDIA GeForce RTX 4070 Ti;
  it reported 4x MSAA and 8x effective anisotropy for the anisotropic panel.
- The maintainer accepted the task after reviewing the generated visual comparison.
