# Scene identities and transforms

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Build `Panda::Scene`, an optional, GUI-independent prototype scene on the direct renderer. Implement
checked object identity, TRS hierarchy and world caching; camera projection belongs
to the renderer and is reusable without this scene.

Non-goal: No renderer materials, importer or physics.

Interface: `Panda::Scene` and checked object/transform operations; finalize API
names during implementation. No public ECS or EnTT requirement.

Paths: optional scene library headers and implementation, `tests/cases/unit/` and directly affected documentation.
Specifications: [Scene API and transforms](../../design/contracts/scene.md#public-scene-boundary), [ownership](../../design/contracts/resources.md#ownership).

Separate identity/lifecycle and transform math into reviewable slices with focused CPU tests.

## Acceptance

CPU stale-handle, reparent/shear and invalid value tests; exercise the scene without
GUI and ensure direct rendering remains usable without the scene library.

Learning: Explain a rotated child under nonuniform parent scale and a rejected keep-world edit.
