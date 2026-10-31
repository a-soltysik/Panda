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

## Current work

The first implementation slice supplies checked perspective and orthographic
projection matrices without a scene. Rendering, primitives, materials, lights,
shadows, MSAA and the cube example remain to be implemented and verified.

## Current verification

- Windows MSVC 14.51.36231: `msvc-fast` built and ran 459 cases: 455 passed and
  four skipped. After the test-only include correction, the three projection cases
  passed again. `msvc-quality`, header isolation and format checks passed.
- WSL Ubuntu GCC 16.0.1: `gcc-quality`, header isolation, format checks and
  `gcc-fast` passed; 451 of 459 cases passed and eight hardware-dependent cases
  skipped. A separate CMake application linked `Panda::Panda` without Tools or
  CUDA and ran with GLM included both before and after the Panda header.
- Both quality builds reported unchecked-index warnings on the constant GLM
  matrix indices and a magic-number warning on the public layout assertion;
  existing analyzer warnings in unrelated files remain.
- No projection performance measurement or rendered-image validation was performed.
  Camera-to-renderer integration and the task's visual acceptance remain open.
