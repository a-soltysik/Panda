# Development plan

The design, build foundation and GLFW window wrapper are accepted. The static core,
shared diagnostics, optional GLFW Tools, a CUDA availability facade, examples and
CPU tests exist; graphics and compute interoperability remain planned. Toolchain
compatibility and tests/API documentation are accepted in the
authorized build-foundation phase.

## Task order

Resume the current task when asked to continue. Later tasks require their listed
prerequisites to be accepted and their phase to be authorized. The
[workflow](workflow.md) defines execution and review; task cards hold acceptance
criteria and concise current evidence.

| Task | Phase | State | Depends on |
| --- | --- | --- | --- |
| [Build foundation](tasks/build-foundation.md) | Build foundation | Accepted | Accepted design |
| [GLFW window wrapper](tasks/window-wrapper.md) | Build foundation | Accepted | Accepted design; explicitly requested wrapper scope |
| [C++ and CUDA toolchain compatibility](tasks/toolchain-compatibility.md) | Build foundation | Accepted | [Build foundation](tasks/build-foundation.md) |
| [Tests and API documentation](tasks/tests-and-api-docs.md) | Build foundation | Accepted | [Build foundation](tasks/build-foundation.md) |
| [Vulkan context and window lifecycle](tasks/vulkan-context.md) | GPU resources | Accepted | [Build foundation](tasks/build-foundation.md), [C++ and CUDA toolchain compatibility](tasks/toolchain-compatibility.md), [Tests and API documentation](tasks/tests-and-api-docs.md) |
| [Allocation, uploads and resource lifetimes](tasks/resource-lifetimes.md) | GPU resources | Planned | [Vulkan context and window lifecycle](tasks/vulkan-context.md) |
| [Frame rendering and benchmark smoke test](tasks/frame-rendering.md) | GPU resources | Planned | [Allocation, uploads and resource lifetimes](tasks/resource-lifetimes.md) |
| [CUDA device matching and shared buffers](tasks/cuda-shared-buffers.md) | CUDA interoperability | Planned | [Frame rendering and benchmark smoke test](tasks/frame-rendering.md) |
| [Vulkan and CUDA synchronization](tasks/gpu-handoff.md) | CUDA interoperability | Planned | [CUDA device matching and shared buffers](tasks/cuda-shared-buffers.md) |
| [Scene identities and transforms](tasks/scene-transforms.md) | First scene | Planned | [Vulkan and CUDA synchronization](tasks/gpu-handoff.md) |
| [First lit and shadowed scene](tasks/first-scene.md) | First scene | Planned | [Scene identities and transforms](tasks/scene-transforms.md) |
| [Materials, lights and shadows](tasks/materials-and-shadows.md) | Renderer features | Planned | [First lit and shadowed scene](tasks/first-scene.md) |
| [Custom shaders and safe reload](tasks/custom-shaders.md) | Renderer features | Planned | [Materials, lights and shadows](tasks/materials-and-shadows.md) |
| [GPU sphere billboards](tasks/sphere-billboards.md) | Renderer features | Planned | [Custom shaders and safe reload](tasks/custom-shaders.md) |
| [Public compute API](tasks/compute-api.md) | Compute integration | Planned | [Vulkan and CUDA synchronization](tasks/gpu-handoff.md), [GPU sphere billboards](tasks/sphere-billboards.md) |
| [Scene persistence and asset resolution](tasks/scene-persistence.md) | Application integration | Planned | [Scene identities and transforms](tasks/scene-transforms.md), [GPU sphere billboards](tasks/sphere-billboards.md) |
| [Static asset import](tasks/asset-import.md) | Application integration | Planned | [Materials, lights and shadows](tasks/materials-and-shadows.md), [Scene persistence and asset resolution](tasks/scene-persistence.md) |
| [Scene tools and safe edits](tasks/scene-tools.md) | Application integration | Planned | [Scene persistence and asset resolution](tasks/scene-persistence.md), [Static asset import](tasks/asset-import.md) |
| [Prototype physics](tasks/prototype-physics.md) | Replacement acceptance | Planned | [Scene identities and transforms](tasks/scene-transforms.md), [Scene tools and safe edits](tasks/scene-tools.md) |
| [Benchmarks and replacement acceptance](tasks/replacement-acceptance.md) | Replacement acceptance | Planned | [Public compute API](tasks/compute-api.md), [Static asset import](tasks/asset-import.md), [Scene tools and safe edits](tasks/scene-tools.md), [Prototype physics](tasks/prototype-physics.md) |

## Phase reviews

The build foundation and GPU resources phase are authorized. Later phases await maintainer review
of the preceding phase's evidence; passing checks alone does not authorize them.
Keep existing task dependencies when working in smaller implementation slices.

| Phase | Evidence required before the next phase |
| --- | --- |
| Build foundation | Windows/Linux build and toolchain probes, CUDA-off independence, C++23/CUDA20 bridge, enforced analysis and test/docs setup |
| GPU resources | Window/swapchain failure paths, allocation and in-flight lifetime checks, two frame slots, RenderDoc capture and initial timestamps |
| CUDA interoperability | Device-matched Windows/Linux sharing, ordered handoff and fault/lifecycle tests; no normal-path CPU copies or global waits |
| First scene | Public-API lit cube, transforms, directional shadow, effective quality settings and maintainer walkthrough |
| Renderer features | Material/shader ABI, all light/shadow types, custom shaders, sphere depth and structural draw/performance checks |
| Compute integration | Public native callback API and ordered mixed-workload/lifecycle tests |
| Application integration | Persistence, import and tools working in an external application without engine edits |
| Replacement acceptance | Restricted physics, full benchmark evidence, documentation and accepted functional replacement |

Tests, documentation and a useful learning example accompany each task. Add features
to the benchmark harness as they become available. A failed required probe stops
dependent work until its design or support decision is resolved.

Reference source stays in `legacy/` until the replacement is accepted and removal is
explicitly requested.
