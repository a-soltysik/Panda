# Custom shaders and safe reload

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Build validated embedded GLSL, a reflected renderer and application resource interface, explicit shadow variants, warmup/cache and transactional reload.

Non-goal: No runtime shader DSL, descriptor arrays or implicit migration.

Interface: shader build integration, programs, resource bindings and explicit shadow variants.

Paths: `cmake/`, `../../../include/core/panda`, `src/shader/`, `shaders/`, `tests/shader/` and directly affected documentation.
Specifications: [Shader integration](../../design/contracts/rendering.md#shader-integration-and-billboard-scope), [shader deployment](../../design/package-and-toolchain.md#shader-and-asset-deployment).

Establish build-time shader validation and layout rejection before adding live
replacement and cache behavior.

## Acceptance

SPIR-V/host offset tests, wrong-layout/foreign-context rejection, missing shadow variant and camera+shadow reload rollback.

Learning: Explain the difference between SPIR-V compilation and device pipeline creation.
