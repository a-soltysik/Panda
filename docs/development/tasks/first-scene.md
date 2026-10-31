# First lit and shadowed rendering

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Add direct application-supplied render data, primitives, built-in material,
directional light/shadow, MSAA fallback and a public-API cube example without a
scene, tools or ECS dependency. The application owns its state and update loop.

Non-goal: No point shadows, custom shader or asset import.

Interface: renderer draw descriptions and a render view; finalize signatures during implementation.

Paths: `include/core/panda/`, `src/renderer/`, `shaders/`, examples, tests and directly affected documentation.
Specifications: [First scene](../../design/contracts/scene.md#first-useful-scene), [renderer](../../design/contracts/rendering.md#first-version-renderer-contract), [resource lifetime](../../design/contracts/resources.md#frame-contexts-and-retirement).

## Acceptance

Compile an external cube example using the renderer without scene or tools; GPU
visual/validation capture of cube, ground, shadow and effective quality on supported devices.

Learning: Explain the default light, shadow depth and effective MSAA choice.
