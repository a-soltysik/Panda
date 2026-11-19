# Custom shaders and safe reload

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Build validated embedded GLSL, reflected application resource bindings, explicit shadow variants,
warmup/cache and transactional reload. Let applications submit procedural
instanced draws directly with a custom program, explicit vertex and instance
counts and GPU buffer bindings.

Non-goal: No runtime shader DSL, descriptor arrays or implicit migration.

Interface: shader build integration, programs, resource bindings, procedural
draws and explicit shadow variants.

Paths: `cmake/`, `../../../include/core/panda`, `src/shader/`, `shaders/`, `tests/shader/` and directly affected
documentation.
Specifications: [Shader integration](../../design/contracts/rendering.md#shader-integration),
[procedural instanced draws](../../design/contracts/rendering.md#procedural-instanced-draws),
[shader deployment](../../design/package-and-toolchain.md#shader-and-asset-deployment).

Establish build-time shader validation and layout rejection before adding live
replacement and cache behavior.

## Acceptance

- Verify SPIR-V/host offsets, wrong-layout and foreign-context rejection,
  missing shadow variant, and camera and shadow reload rollback.
- Provide a runnable graphics-only example with application-owned opaque
  sphere impostor shaders, a uniform buffer and instance storage buffers.
  Compare sphere depth with a mesh reference in the camera and all shadow
  view types, including the off-axis, near-plane and inside cases. Verify
  uniform scaling of the visualization and independence from the domain pose.
- Check 10,000, 100,000 and 1,000,000 instances. Each valid batch produces
  one draw per applicable pass and no per-instance CPU object or per-frame
  CPU copy of instance data.
  Verify zero-count behavior, changing bindings and retention through GPU
  completion.

Learning: Explain the difference between SPIR-V compilation and device pipeline creation.
