# Materials, lights and shadows

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Implement per-mesh vertex layouts, standard material maps/alpha/faces, SSBO light
budget, spot/point atlas views, color space, tone map and pipeline keys.

Non-goal: No custom material DSL or IBL.

Interface: Material and light data, shadow views and renderer pipeline keys; finalize public names and GPU layout during
implementation.

Paths: `../../../include/core/panda`, `src/renderer/`, `shaders/`, `tests/gpu/` and directly affected documentation.
Specifications: [Renderer layout and materials](../../design/contracts/rendering.md#first-version-renderer-contract), [transform conventions](../../design/contracts/scene.md#transforms-and-camera-conventions).

Verify material/host layouts first, then lighting/shadows, then quality and pipeline
behavior. Each slice needs its relevant visual or ABI evidence.

## Acceptance

ABI/visual cases for position/color-only and lit mesh layouts, missing tangent with
normal-map fallback, 65 lights, mask shadows, negative scale, tangent sign,
spot/point shadows and format fallback.

The first-scene shadow uses a fixed 2048 by 2048 map over 30 m and 3x3 PCF, which
visibly aliases at its edges. This task must tune or replace that filtering and
coverage strategy. GPU captures at representative camera distances must show a
clear improvement without visible self-shadow acne or detached shadows; record the
effective resolution/filtering and any remaining limits.

Learning: Inspect a tinted textured surface and its shadow in RenderDoc.
