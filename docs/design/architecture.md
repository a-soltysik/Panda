# Architecture

This is the accepted design. [The plan](../development/PLAN.md) records implementation
progress; the core, window wrapper and CUDA availability facade are implemented.
Generic external-buffer creation and CUDA import are in progress.

## Modules and responsibilities

| Target | Owns | Must not require |
| --- | --- | --- |
| `Panda::Panda` | Vulkan context/resources, renderer, scenes and built-in assets | CUDA, importers, tools, physics |
| `Panda::Import` | Import adapter and conversion into Panda assets | Tools, CUDA, physics |
| `Panda::Tools` | Optional GUI, inspection and transform gizmos | Importers, CUDA, physics |
| `Panda::Cuda` | Optional CUDA device matching and external-buffer import | Application solver code and simulation schedule |
| `Panda::Physics` | Restricted CPU physics and scene adapter | CUDA, importers, tools |

Add-ons depend on the core, never the reverse. Split internal targets only for a
concrete ownership or compilation responsibility. The [package specification](package-and-toolchain.md)
owns public headers, dependency selection and build switches.

## Design choices

- Use Vulkan 1.3 and one forward renderer. A small engine does not need a generic
  RHI, general render graph or a second rendering implementation.
- Use checked Scene operations backed by EnTT, plus an opt-in native ECS extension.
  Composition and value snapshots avoid a required object inheritance hierarchy
  and long-lived references into relocatable component storage.
- A GPU cloud is one renderable with buffers and a conservative bound. Particle
  count must not create proportional CPU entities, updates or draw calls.
- Context-bound resource handles retain GPU inputs until completion. Ownership
  and synchronization are separate obligations; see [resources](contracts/resources.md).
- The application owns compute algorithms, simulation time and update frequency.
  Panda provides graphics and explicit resource/handoff building blocks; it does not
  schedule persistent compute callbacks or translate kernels between backends.
- Use C++23 and explicit recoverable errors, with a narrow C++20 CUDA adapter.
  Verify one compatible host toolchain per OS rather than assuming cross-compiler ABI.
- Ship static source libraries through CPM/add_subdirectory. Embed built-in SPIR-V
  and give application shaders the same build route. Applications own asset roots
  and deployment; they do not patch Panda or depend on a working directory.

These choices prioritize a small understandable engine. The detailed specifications
fix behavior and failure paths; a failing implementation probe requires revisiting
its affected decision before dependent expansion.

## Frame flow

1. Poll input and collect UI/application intents.
2. Apply scene edits and application-controlled simulation/physics steps.
3. Prepare frame-local scene data and retain required resources.
4. The application may request explicit GPU work using its chosen simulation schedule.
5. Render shadows and camera passes, then present.
6. Reclaim resources after their last GPU use.

Use one graphics/compute queue, a separate presentation queue only when needed,
two frame slots. The [compute contract](contracts/compute.md) defines the
application-owned schedule and the required handoff guarantees. No general
asynchronous dependency scheduler is promised.

## Detailed specifications

[Scene](contracts/scene.md) owns entities, transforms, persistence, import and tools.
[Rendering](contracts/rendering.md) owns materials, lighting, shader ABI and sphere
billboards. [Physics](contracts/physics.md) is deliberately limited CPU prototyping.
[Quality](../development/quality.md) applies to all implementation work.

Reference code in [legacy](../../legacy/README.md) supplies examples, not compatibility
constraints or runtime dependencies. Reuse an idea only after checking its contract.
