# Development plan

The design, build foundation and GLFW window wrapper are accepted. The static core,
shared diagnostics, optional GLFW Tools, a CUDA availability facade, examples and
CPU tests exist. Generic external-memory buffer creation, paired timeline-semaphore
export and CUDA import are implemented. Ordered cross-API handoff is accepted with
Windows GPU verification and WSL build/test evidence;
application-level compute integration remains incomplete. Toolchain compatibility
and tests/API documentation are accepted in the authorized build-foundation phase.
After renderer features, application integration first delivers a graphics-only
consumer path; compute and full replacement acceptance are later goals, not
prerequisites for using Panda as a graphics engine.

## Task order

Resume the current task when asked to continue. Later tasks require their listed
prerequisites to be accepted and their phase to be authorized. The
[workflow](workflow.md) defines execution and review; task cards hold acceptance
criteria and concise current evidence.

| Task                                                                      | Phase                   | State       | Depends on                                                                                                                                                                                                                                                                               |
|---------------------------------------------------------------------------|-------------------------|-------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| [Build foundation](tasks/build-foundation.md)                             | Build foundation        | Accepted    | Accepted design                                                                                                                                                                                                                                                                          |
| [GLFW window wrapper](tasks/window-wrapper.md)                            | Build foundation        | Accepted    | Accepted design; explicitly requested wrapper scope                                                                                                                                                                                                                                      |
| [C++ and CUDA toolchain compatibility](tasks/toolchain-compatibility.md)  | Build foundation        | Accepted    | [Build foundation](tasks/build-foundation.md)                                                                                                                                                                                                                                            |
| [Tests and API documentation](tasks/tests-and-api-docs.md)                | Build foundation        | Accepted    | [Build foundation](tasks/build-foundation.md)                                                                                                                                                                                                                                            |
| [Vulkan context and window lifecycle](tasks/vulkan-context.md)            | GPU resources           | Accepted    | [Build foundation](tasks/build-foundation.md), [C++ and CUDA toolchain compatibility](tasks/toolchain-compatibility.md), [Tests and API documentation](tasks/tests-and-api-docs.md)                                                                                                      |
| [Allocation, uploads and resource lifetimes](tasks/resource-lifetimes.md) | GPU resources           | Accepted    | [Vulkan context and window lifecycle](tasks/vulkan-context.md)                                                                                                                                                                                                                           |
| [Frame rendering and benchmark smoke test](tasks/frame-rendering.md)      | GPU resources           | Accepted    | [Allocation, uploads and resource lifetimes](tasks/resource-lifetimes.md)                                                                                                                                                                                                                |
| [CUDA device matching and shared buffers](tasks/cuda-shared-buffers.md)   | CUDA interoperability   | Accepted    | [Frame rendering and benchmark smoke test](tasks/frame-rendering.md)                                                                                                                                                                                                                     |
| [Vulkan and external backend handoff](tasks/gpu-handoff.md)               | CUDA interoperability   | Accepted    | [CUDA device matching and shared buffers](tasks/cuda-shared-buffers.md)                                                                                                                                                                                                                  |
| [First lit and shadowed rendering](tasks/first-scene.md)                  | First scene             | Accepted    | [Vulkan and external backend handoff](tasks/gpu-handoff.md)                                                                                                                                                                                                                              |
| [Optional scene identities and transforms](tasks/scene-transforms.md)     | First scene             | Accepted    | [First lit and shadowed rendering](tasks/first-scene.md)                                                                                                                                                                                                                                 |
| [Materials, lights and shadows](tasks/materials-and-shadows.md)           | Renderer features       | In progress | [First lit and shadowed scene](tasks/first-scene.md)                                                                                                                                                                                                                                     |
| [Offscreen rendering and image readback](tasks/offscreen-rendering.md)    | Renderer features       | Planned     | [Materials, lights and shadows](tasks/materials-and-shadows.md)                                                                                                                                                                                                                          |
| [Presentation modes and frame pacing](tasks/presentation-modes.md)        | Renderer features       | Planned     | [Frame rendering and benchmark smoke test](tasks/frame-rendering.md), [First lit and shadowed rendering](tasks/first-scene.md)                                                                                                                                                           |
| [Custom shaders and safe reload](tasks/custom-shaders.md)                 | Renderer features       | Planned     | [Materials, lights and shadows](tasks/materials-and-shadows.md)                                                                                                                                                                                                                          |
| [GPU sphere billboards](tasks/sphere-billboards.md)                       | Renderer features       | Planned     | [Custom shaders and safe reload](tasks/custom-shaders.md)                                                                                                                                                                                                                                |
| [Graphics-only consumer integration](tasks/graphics-only-consumer.md)     | Application integration | Planned     | [Materials, lights and shadows](tasks/materials-and-shadows.md), [Presentation modes and frame pacing](tasks/presentation-modes.md)                                                                                                                                                      |
| [Scene persistence and asset resolution](tasks/scene-persistence.md)      | Application integration | Planned     | [Scene identities and transforms](tasks/scene-transforms.md), [GPU sphere billboards](tasks/sphere-billboards.md)                                                                                                                                                                        |
| [Static asset import](tasks/asset-import.md)                              | Application integration | Planned     | [Materials, lights and shadows](tasks/materials-and-shadows.md), [Scene persistence and asset resolution](tasks/scene-persistence.md)                                                                                                                                                    |
| [Scene tools and safe edits](tasks/scene-tools.md)                        | Application integration | Planned     | [Scene persistence and asset resolution](tasks/scene-persistence.md), [Static asset import](tasks/asset-import.md)                                                                                                                                                                       |
| [Application-driven compute integration](tasks/compute-api.md)            | Compute integration     | Planned     | [Vulkan and external backend handoff](tasks/gpu-handoff.md), [GPU sphere billboards](tasks/sphere-billboards.md)                                                                                                                                                                         |
| [Prototype physics](tasks/prototype-physics.md)                           | Replacement acceptance  | Planned     | [Scene identities and transforms](tasks/scene-transforms.md), [Scene tools and safe edits](tasks/scene-tools.md)                                                                                                                                                                         |
| [Benchmarks and replacement acceptance](tasks/replacement-acceptance.md)  | Replacement acceptance  | Planned     | [Application-driven compute integration](tasks/compute-api.md), [Graphics-only consumer integration](tasks/graphics-only-consumer.md), [Static asset import](tasks/asset-import.md), [Scene tools and safe edits](tasks/scene-tools.md), [Prototype physics](tasks/prototype-physics.md) |

## Phase reviews

The build foundation, GPU resources, CUDA interoperability and First scene phases
are authorized. Later phases await maintainer review of the preceding phase's
evidence; passing checks alone does not authorize them. Application integration
precedes compute so a graphics-only consumer does not wait for CUDA work. Full
replacement acceptance remains downstream of both. Keep existing task dependencies
when working in smaller implementation slices.

| Phase                   | Evidence required before the next phase                                                                                                                                                      |
|-------------------------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| Build foundation        | Windows/Linux build and toolchain probes, CUDA-off independence, C++23/CUDA20 bridge, enforced analysis and test/docs setup                                                                  |
| GPU resources           | Window/swapchain failure paths, allocation and in-flight lifetime checks, two frame slots, RenderDoc capture and initial timestamps                                                          |
| CUDA interoperability   | Device-matched buffer sharing, Windows GPU-side ordered handoff proof, WSL builds/tests with explicit capability skips, and fault/lifecycle tests; no normal-path CPU copies or global waits |
| First scene             | Direct-renderer lit cube, transforms, directional shadow, effective quality settings, application-owned mesh collection (no `Scene` class) and maintainer walkthrough                        |
| Renderer features       | Material/shader ABI, light/shadow types, presentation-mode selection/reporting, custom shaders, sphere depth and draw/performance checks                                                     |
| Application integration | Graphics-only `Panda::Panda` consumer, followed by persistence, import and tools in an external application; CUDA is disabled for the graphics-only path                                     |
| Compute integration     | Application-driven batched compute workload and mixed-workload/lifecycle tests; no recurring engine scheduler                                                                                |
| Replacement acceptance  | Restricted physics, full benchmark evidence, documentation and accepted functional replacement                                                                                               |

Tests, documentation and a useful learning example accompany each task. Add features
to the benchmark harness as they become available. A failed required probe stops
dependent work until its design or support decision is resolved.

Reference source stays in `legacy/` until the replacement is accepted and removal is
explicitly requested.
