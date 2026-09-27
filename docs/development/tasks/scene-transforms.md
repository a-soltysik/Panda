# Scene identities and transforms

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Implement checked Entity/ObjectId operations, EnTT extension, TRS hierarchy, world caching and camera projection rules.

Non-goal: No renderer materials, importer or physics.

Interface: Scene/Entity/ObjectId/Transform operations and opt-in panda/Ecs.hpp.

Paths: `../../../include/core/panda`, `../../../include/core/panda`, `src/scene/`, `tests/unit/` and directly affected documentation.
Specifications: [Scene API and transforms](../../design/contracts/scene.md#public-scene-boundary), [ownership](../../design/contracts/resources.md#ownership).

Separate identity/lifecycle, transform math and native ECS integration into
reviewable slices with focused CPU tests.

## Acceptance

CPU stale-handle, reparent/shear, invalid value, camera/Y/depth and native component cleanup tests.

Learning: Explain a rotated child under nonuniform parent scale and a rejected keep-world edit.
