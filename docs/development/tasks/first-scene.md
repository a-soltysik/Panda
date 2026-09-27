# First lit and shadowed scene

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Add cached primitives, Studio/Empty defaults, built-in material, directional light/shadow, MSAA fallback and a public-API cube example.

Non-goal: No point shadows, custom shader or asset import.

Interface: SceneOptions, Scene::add_primitive and Context::render(Scene&).

Paths: `src/scene/`, `src/renderer/`, `shaders/`, `examples/cube/`, `tests/gpu/` and directly affected documentation.
Specifications: [First scene](../../design/contracts/scene.md#first-useful-scene), [renderer](../../design/contracts/rendering.md#first-version-renderer-contract), [resource lifetime](../../design/contracts/resources.md#frame-contexts-and-retirement).

## Acceptance

Compile external cube example; GPU visual/validation capture of cube, ground, shadow and effective quality on supported devices.

Learning: Explain the default light, shadow depth and effective MSAA choice.
