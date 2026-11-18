# Architecture

This is the accepted design. [The plan](../development/PLAN.md) records
implementation progress and maintainer acceptance. Core rendering, the optional
scene and window modules, and CUDA external-buffer import and ordered handoff
are implemented.

## Modules and responsibilities

| Target              | Owns                                                                          | Must not require                                  |
|---------------------|-------------------------------------------------------------------------------|---------------------------------------------------|
| `Panda::Panda`      | Vulkan context/resources, mesh data, shape generators, renderer and CPU math  | Scene, CUDA, importers, tools, physics            |
| `Panda::Scene`      | Optional prototype scene, identity, transforms and renderer-input preparation | GUI, CUDA, physics, application ECS               |
| `Panda::Import`     | Import adapter and conversion into Panda assets                              | Tools, CUDA, physics                              |
| `Panda::Tools`      | Optional GUI, inspection and transform gizmos                                | Importers, CUDA, physics                          |
| `Panda::Cuda`       | Optional CUDA device matching, external-buffer import and ordered handoff     | Application solver code and simulation schedule   |
| `Panda::Physics`    | Restricted CPU physics and scene adapter                                     | CUDA, importers, tools                            |
| `Panda::All`        | Interface aggregation of production targets enabled by the configuration    | Enabling modules or discovering dependencies      |

Common errors, diagnostics and shared value types live in the internal `panda_base`
library. Public modules bring it in transitively; it is not a separately selectable
target and is not part of `Panda::All`. Core owns the mesh payload, vertex layout,
shape generators and CPU math. These features share one public target; shape
generators live in `src/core/geometry`. Scene uses Core math, while the other
add-ons depend on Core. `Panda::All` is only an application convenience; internal
libraries link their specific dependencies. Split internal targets only for a
concrete ownership or compilation responsibility. The
[package specification](package-and-toolchain.md) owns public headers,
dependency selection and build switches.

Scene-editing tools depend on `Panda::Scene`; the existing window wrapper
remains usable without it. Select target boundaries when implementing the editor
so this dependency is limited to scene-editing functionality. A scene runs
without tools.

## Design choices

- Use Vulkan 1.3 and one forward renderer. A small engine does not need a
  generic RHI, general render graph or a second rendering implementation.
- Applications own their data and may render directly. An optional scene library
  supplies a convenient managed scene for prototyping, independently of the GUI.
  Neither the renderer nor scene API requires an application ECS. Storage and
  any future ECS integration are implementation decisions, not current API
  promises.
- A GPU cloud is one renderable with buffers and a conservative bound. Particle
  count must not create proportional CPU entities, updates or draw calls.
- Engine-bound resource handles retain GPU inputs until completion. Ownership
  and synchronization are separate obligations; see
  [resources](contracts/resources.md).
- `Engine::Impl` owns the Vulkan data and concrete service implementations
  directly. Its heap-stable `DeviceContext` contains only the loader,
  instance/surface, physical/logical device and diagnostics; public service
  facades borrow their narrow dependencies. `EngineCreateInfo` exists only while
  creation assembles and transfers these owners.
- The application owns compute algorithms, simulation time and update frequency.
  Panda provides graphics and explicit resource/handoff building blocks; it does
  not schedule persistent compute callbacks or translate kernels between
  backends.
- Use C++23 and explicit recoverable errors, with a narrow C++20 CUDA adapter.
  Verify one compatible host toolchain per OS rather than assuming
  cross-compiler ABI.
- Ship static source libraries through CPM/add_subdirectory. Embed built-in
  SPIR-V and give application shaders the same build route. Applications own
  asset roots and deployment; they do not patch Panda or depend on a working
  directory.

These choices prioritize a small understandable engine. The detailed
specifications fix behavior and failure paths; a failing implementation probe
requires revisiting its affected decision before dependent expansion.

## Frame flow

1. Poll input and collect UI/application intents.
2. Apply application updates and any optional scene edits or physics steps.
3. Prepare render data from application state or an optional scene and retain
   resources.
4. The application may request explicit GPU work using its chosen simulation
   schedule.
5. Render shadows and camera passes, then present.
6. Reclaim resources after their last GPU use.

Use one graphics/compute queue, a separate presentation queue only when needed,
two frame slots. The [compute contract](contracts/compute.md) defines the
application-owned schedule and the required handoff guarantees. No general
asynchronous dependency scheduler is promised.

## Detailed specifications

[Scene](contracts/scene.md) owns entities, transforms, persistence, import and
tools. [Rendering](contracts/rendering.md) owns materials, lighting, shader ABI
and sphere billboards. [Physics](contracts/physics.md) is deliberately limited
CPU prototyping. [Quality](../development/quality.md) applies to all
implementation work.

Reference code in [legacy](../../legacy/README.md) supplies examples, not
compatibility constraints or runtime dependencies. Reuse an idea only after
checking its contract.
