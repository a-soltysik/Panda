# Shared HDR render path

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Render window and offscreen views into a shared linear HDR path for v1.
Accumulate lighting and blend transparent geometry before tone mapping.
Resolve MSAA in HDR, then apply exposure, tone mapping and SDR encoding
in one final pass. Window output and offscreen readback remain SDR.

Specifications: [Materials, faces, and output](../../design/contracts/rendering.md#materials-faces-and-output),
[offscreen output](../../design/contracts/rendering.md#offscreen-output).

## Acceptance

- Use the specified HDR color format and check format, usage and sample-count
  support before using it. Verify single-sample rendering and MSAA resolve.
- Verify that a partially transparent surface over a bright background is
  blended in linear HDR and tone-mapped once. Check equivalent SDR colors
  in window and offscreen output for the same view. When the skybox is
  enabled, verify that its radiance is composited before the final pass.

Learning: Trace one bright transparent pixel from linear HDR blending through
MSAA resolve and tone mapping. Explain why window capture records the SDR pass
separately for the swapchain and readback target.

## Verification

- Windows `msvc-fast`: full build passed; CTest had 0 failures and 5 skips
  (2 output-device failure cases and 3 non-coherent-memory cases).
- WSL Ubuntu `gcc-fast`: full build passed; CTest had 0 failures and 8 skips
  (5 CUDA interop and 3 non-coherent-memory cases unavailable in this environment).
- `check_docs.py` checked 90 Markdown files and the task queue. `docs-checks`
  validated 43 Mermaid diagrams in 94 files.
- `git diff --check` passed.
