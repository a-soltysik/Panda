# Panda documentation

Panda is being rewritten as a small Vulkan engine with optional CUDA integration.
The design is accepted; implementation is at the build foundation. A minimal C++23
static library, shared logger/checked diagnostics, a Tools window, an optional
CUDA availability facade and examples exist. Rendering, compute integration and the broader
application modules remain planned. See [the plan](development/PLAN.md) for current work and approvals.

## Where to start

| Question | Read |
| --- | --- |
| What are we building? | [Scope](design/scope.md), then [architecture](design/architecture.md) |
| What should I implement next? | [Plan and tasks](development/PLAN.md) |
| How should I work and verify it? | [Workflow](development/workflow.md), [quality](development/quality.md) |
| What code style does the maintainer prefer? | [Project conventions](development/conventions.md) |
| How are tests divided and dependencies mocked? | [Test organization](development/testing.md) |
| How is Panda consumed and configured? | [Package and toolchain](design/package-and-toolchain.md) |
| Who owns resources and completion? | [Resources and frames](design/contracts/resources.md) |
| How do Vulkan and CUDA cooperate? | [Compute](design/contracts/compute.md) |
| How do scenes, import and tools behave? | [Scene](design/contracts/scene.md) |
| What must materials and shaders implement? | [Rendering](design/contracts/rendering.md) |
| What physics is included? | [Prototype physics](design/contracts/physics.md) |
| How do we measure and learn? | [Benchmarks](development/benchmarking.md), [learning](development/learning.md) |
| Where are useful examples? | [Empty-window example](../examples/simple_scene/README.md), [CUDA connection](../examples/cuda_connection/README.md), [reference code](../legacy/README.md) |

Read only the sections relevant to a change. Specifications describe intended
behavior and invariants; task cards record delivery criteria and current evidence.
API sketches are not available commands or a claim of implementation. Generated
API documentation belongs to the build output, not this source tree.

Maintain these documents in place using [the workflow](development/workflow.md#maintain-the-documentation).
Keep current rules and useful rationale; remove obsolete material instead of adding
release-note histories or contradictory corrections.
