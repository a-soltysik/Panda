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
  in window and offscreen output for the same view.

Learning: Compare blending before and after tone mapping for the same two colors.
