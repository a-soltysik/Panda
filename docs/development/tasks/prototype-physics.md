# Prototype physics

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Implement the approved sphere/box/plane model, fixed stepping, filters, contacts and optional Scene binding.

Non-goal: No general rigid-body engine, joints or CCD.

Interface: PhysicsWorld/Body/Contact operations and optional Scene binding.

Paths: `../../../include/core/panda`, `src/physics/`, `tests/physics/`, `examples/physics/` and directly affected documentation.
Specifications: [Physics](../../design/contracts/physics.md), [transforms](../../design/contracts/scene.md#transforms-and-camera-conventions).

## Acceptance

CPU-only shape/contact/filter/teleport/kinematic tests, deletion/scale rejection and a visible tunneling case.

Learning: Explain pose authority and why a faster sphere can tunnel.
