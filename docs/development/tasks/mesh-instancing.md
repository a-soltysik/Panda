# Mesh instancing

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Render repeated ordinary meshes in batches with per-instance transforms and
optional tint. Support the built-in material path in the camera and shadow
passes. The application supplies instance data through the direct renderer.

Specifications: [Rewrite scope](../../design/scope.md#requirements),
[instancing and billboards](../../design/contracts/rendering.md#instancing-and-billboards).

## Acceptance

- Verify that compatible copies of one mesh use an instanced draw in each
  applicable pass and that draw counts scale with batches rather than copies.
- Compare each instance's camera placement, tint and shadow placement with
  the equivalent individual draws.

Learning: Explain which changes require another batch and which fit in the
per-instance data.
