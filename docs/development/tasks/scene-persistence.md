# Scene persistence and asset resolution

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Implement strict version-one JSON load/save, stable IDs, scene-owned material
sharing, application asset resolution and atomic replacement.

Non-goal: No solver configuration, arbitrary ECS serialization or GPU checkpoint.

Interface: scene persistence, material registration and application asset resolution;
choose public names and signatures during implementation.

Paths: optional scene library headers and implementation, persistence tests and directly affected documentation.
Specifications: [Persistence](../../design/contracts/scene.md#persistence-import-and-tools), [shader resources](../../design/contracts/rendering.md#programs-materials-and-application-resources).

Implement document validation and reference resolution before publishing scenes
or writing files; verify failure atomicity at both boundaries.

## Acceptance

Round-trip edited shared materials and stable IDs. Test missing resolvers,
invalid versions, cycles and paths, and failed-write rollback. Verify that an
ordinary visualization anchor survives load and can be resolved by the application.

Learning: Reload a simulation domain and its visualization anchor and identify
what the application reconstructs.
