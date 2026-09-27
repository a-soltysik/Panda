# Rewrite scope

Accepted product requirements. The [plan](../development/PLAN.md) records implementation progress.

## Purpose

Panda is a small, extensible Vulkan graphics engine and a set of practical tools
for C++ applications. It should make a lit, shadowed scene easy to create while
remaining useful for GPU simulations and custom rendering work.

The project favors composable building blocks and clear extension examples over
a large editor, game framework, or abstraction over multiple graphics APIs.
The legacy Panda engine is reference material, not a compatibility constraint.

## Requirements

| Area | Requirement |
| --- | --- |
| Platforms | Support Windows and Linux; render on capable NVIDIA, AMD, and Intel GPUs. Require Vulkan 1.3 and explicitly check required features and limits. Windowed rendering also requires KHR or equivalent EXT swapchain maintenance1 with its dependencies. |
| Language | Target C++23 for Panda, with a narrow C++20 boundary for CUDA translation units. Validate real compiler/toolkit combinations before declaring support. |
| Packaging | Allow consumption through CPM without copying engine code or adding application-specific logic to Panda. Optional modules must not become core dependencies. |
| Rendering | Implement rendering, lighting, shadows, and anti-aliasing within Panda using Vulkan. Vulkan-Hpp and SPIRV-Reflect are permitted. |
| Defaults | Offer useful default camera, lighting, shadows, and material settings from the first cube, with an explicit empty-scene alternative. |
| Scenes | Support editable scenes defined in C++ or loaded from files, hierarchy, runtime transforms, reusable assets, and optional parameter panels/gizmos. |
| Lights | Support a few to a few dozen lights without a small fixed shader light array. Handle color-only, textured, and instanced materials consistently. |
| Instancing | Support efficient instanced geometry and GPU-resident billboard clouds. Particle count must not imply one entity, CPU update, or draw call per particle. |
| Shaders | Make complete custom shaders practical, including build integration, layout validation, and an explicit shadow-pass contract. No material DSL is required. |
| Compute availability | Provide Vulkan compute independently of optional CUDA. CUDA is disabled by default at CMake configuration time. Both may participate in one application. |
| Compute boundary | Keep compute orchestration thin: schedule native callbacks, batch work, and synchronize declared shared resources. Algorithms belong to the application. |
| Persistence | Persist scene setup and references, not a hidden checkpoint of all GPU simulation state. Domain-specific configuration remains application-owned. |
| Import | Provide optional model import through a support library, with glTF/GLB preferred and common static formats available at runtime. |
| Physics | Provide optional, deliberately small CPU physics for prototyping: gravity, linear motion, and a restricted set of collisions. |
| Quality | Use explicit recoverable errors, clear ownership, strong static analysis, documented public APIs, and readable modern C++. |
| Benchmarks | Include an automated benchmark executable and reproducible profiles; make correctness, performance, and learning evidence part of delivery. |
| Reference code | Keep legacy Panda and the selected drip engine snapshot, including supporting context, in the repository until the replacement is working and accepted. Later removal requires explicit maintainer approval. |

## Representative applications

- A small scene with imported static models, several lights, shadows, and editable transforms.
- A GPU simulation visualized as instanced billboards inside a movable and resizable domain.
- A simple driving visualization with application-owned vehicle behavior and configuration.
- A graphics experiment using custom shaders and optional Vulkan/CUDA computation.

These scenarios guide integration checks. They do not require a fluid solver,
vehicle simulator or general-purpose game framework.

## Explicit exclusions for the first version

- macOS and a Vulkan 1.0-1.2 compatibility renderer.
- Other graphics backends, a generic RHI, ray tracing, and an editor comparable to a game engine.
- Skeletal animation, animation clips, skinning, and morph-target systems.
- Fluid, hair, cloth, soft-body, or vehicle solvers.
- General-purpose rigid-body physics, robust stacks, joints, or mandatory physics middleware.
- Automatic translation between CUDA and Vulkan compute, automatic CPU fallback, or runtime migration of algorithm state.
- Mandatory reflection-based object systems, a shader graph, or a custom material language.
- Full simulation checkpoints and serialization of individual GPU-only particles.
- Unbounded scene scale, unrestricted transparency correctness, or a promise of high performance on every GPU.

Application-driven transforms and custom GPU deformation remain possible despite
the absence of an animation subsystem. Applications may integrate more advanced
physics or simulation libraries without modifying the rendering core.

## Simplicity rule

Add an abstraction when it has a concrete responsibility and removes repeated
work for the stated use cases. Do not hide backend-specific algorithm requirements
behind a nominally universal interface. Prefer a small documented extension point
over a system that tries to discover an application's intent.

The acceptance criteria in [the plan](../development/PLAN.md) and
[benchmark specification](../development/benchmarking.md) make these goals testable.

## Reference code

[Legacy Panda and drip](../../legacy/README.md) remain available for source inspection.
They are outside new targets and ordinary modernization work; removal requires an
accepted replacement and an explicit maintainer request.
