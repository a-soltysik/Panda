# C++ and CUDA toolchain compatibility

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Probe one host compiler per OS, std::expected and GLM/Hpp coexistence, a plain-data CUDA20 adapter and a tiny NVCC producer. Run a CUDA-enabled binary with no usable driver/device.

Non-goal: No shared allocation or frame scheduler.

Interface: Panda::Cuda facade, private C++20 plain-data adapter and application CUDA20 kernel boundary.

Paths: `cmake/`, `src/cuda/`, `../../../include/core/panda`, `tests/toolchain/` and directly affected documentation.
Specifications: [Package](../../design/package-and-toolchain.md#c23-and-cuda20-with-one-host-toolchain), [compute availability](../../design/contracts/compute.md#interoperability).

## Acceptance

Record exact compiler, SDK, toolkit and runtime linkage on Windows/Linux; build/link/run or report unsupported. Resolve a failed probe in the package specification before GPU expansion.

Learning: Show why the CUDA translation unit cannot include the C++23 engine facade.
