# Materials, lights and shadows

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Implement standard material maps/alpha/faces, SSBO light budget, spot/point atlas views, color space, tone map and pipeline keys.

Non-goal: No custom material DSL or IBL.

Interface: MaterialDesc/Material, LightRecord/ShadowView ABI and renderer pipeline keys.

Paths: `../../../include/core/panda`, `src/renderer/`, `shaders/`, `tests/gpu/` and directly affected documentation.
Specifications: [Renderer layout and materials](../../design/contracts/rendering.md#first-version-renderer-contract), [transform conventions](../../design/contracts/scene.md#transforms-and-camera-conventions).

Verify material/host layouts first, then lighting/shadows, then quality and pipeline
behavior. Each slice needs its relevant visual or ABI evidence.

## Acceptance

ABI/visual cases for 65 lights, mask shadows, negative scale, tangent sign, spot/point shadows and format fallback.

Learning: Inspect a tinted textured surface and its shadow in RenderDoc.
