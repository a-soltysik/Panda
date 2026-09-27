# Scene persistence and asset resolution

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Implement strict version-one JSON load/save, stable IDs, scene-owned material sharing, app asset resolver, cloud placeholder and atomic replacement.

Non-goal: No solver configuration, arbitrary ECS serialization or GPU checkpoint.

Interface: Scene::save/load, Scene::register_material and AssetResolver.

Paths: `../../../include/core/panda`, `src/scene/serialization/`, `tests/persistence/` and directly affected documentation.
Specifications: [Persistence](../../design/contracts/scene.md#persistence-import-and-tools), [shader resources](../../design/contracts/rendering.md#programs-materials-and-application-resources).

Implement document validation and reference resolution before publishing scenes
or writing files; verify failure atomicity at both boundaries.

## Acceptance

Round-trip edited shared material, IDs/cloud, missing resolver, invalid version/cycle/path and failed-write rollback.

Learning: Reload a cloud/domain setup and identify what the application must reconstruct.
