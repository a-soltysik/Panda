# Vulkan and external backend handoff

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Design and implement an explicit GPU-side handoff for a Panda-owned external
buffer, starting with `Panda::Cuda`. Preserve the generic external-memory boundary
in Panda Core. The application chooses when to submit compute work; this task does
not add a persistent callback registry, frame scheduler or general task graph.

Non-goal: No CPU copies, routine global waits, backend translation, or simulation
step policy inside Panda.

Interface: To be selected from the Vulkan/CUDA capabilities supported by the target
platforms. The API must make producer and consumer order explicit and keep CUDA
types inside the optional add-on.

Paths: `include/core/panda/`, `include/cuda/panda/cuda/`, `src/core/vulkan/`,
`src/cuda/`, tests, and directly affected documentation.
Specifications: [Compute and external buffers](../../design/contracts/compute.md),
[resource ownership](../../design/contracts/resources.md#lifetime-is-not-synchronization).

## Acceptance

Prove two consecutive producer-to-consumer handoffs on Windows and Linux with
tagged GPU data, Vulkan synchronization validation and CUDA ordering inspection.
Cover Vulkan-to-CUDA and CUDA-to-Vulkan access, failure before each submitted wait,
resource retention during use, and cleanup. Record that no steady-state CPU copy or
device/queue-wide wait is needed.

Learning: Trace a buffer's previous Vulkan use, CUDA producer and next Vulkan read;
explain why a shared OS handle and a live C++ owner are not synchronization.
