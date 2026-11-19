# Environment lighting

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Add opt-in image-based lighting (IBL) to built-in Lit materials for v1. Accept
one environment per view as two application-supplied, preprocessed linear HDR
cubemaps. One stores diffuse irradiance and the other stores specular
radiance filtered by roughness across mip levels. This task includes cubemap
upload and sampling. Applications prepare environment images offline.

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

Learning: Compare how the same environment affects a smooth metal and a rough
dielectric.
