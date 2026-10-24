# CUDA device matching and shared buffers

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Provide backend-neutral external-memory buffers in Panda Core and an optional
`Panda::Cuda` adapter that matches Vulkan/CUDA devices by UUID and imports one
dedicated buffer allocation. Handle ownership, allocation size, logical mapped
range and resource lifetime are part of this task.

Non-goal: No compute callback/submission API, Vulkan/CUDA synchronization, solver,
CPU bounce path or image sharing. Importing memory does not make cross-API access safe.

Interface: `ContextOptions::ExternalBufferSupport`,
`ContextDeviceInfo::externalBufferExportEnabled`, `Context::createExternalBuffer`, `ExternalBuffer`,
`CudaSession::create`, `CudaSession::importBuffer` and
`SharedCudaBuffer::buffer`.

Paths: `include/core/panda/`, `include/cuda/panda/cuda/`, `src/core/vulkan/external/`,
`src/cuda/`, `tests/cases/` and directly affected documentation.
Specifications: [Compute and external buffers](../../design/contracts/compute.md),
[resource ownership](../../design/contracts/resources.md#lifetime-is-not-synchronization).

## Acceptance

Check matching-device selection, export/import and one-time mapping on an available
CUDA GPU, handle ownership, invalid requests, unsupported devices/usages, and
CUDA-unavailable startup without breaking ordinary Vulkan rendering. For this
task, Windows GPU integration plus Linux quality/build/test coverage in WSL is
sufficient; a native Linux GPU run is not required.

Current evidence: Windows `msvc-quality` and `msvc-fast` builds passed, as did a
CUDA kernel compile in `msvc-development` with `/Zi` and without a warning
suppression. Public header isolation, format checks and Clang CUDA analysis passed.
Windows CTest passed 433 tests and skipped four: one expected benchmark skip and
three noncoherent-memory variants unavailable on the selected device. The
external-buffer CUDA import test ran and passed on the local GPU. Linux
`gcc-quality`, public-header and format checks, Clang CUDA analysis, API docs build
and CTest passed; four tests were skipped, including the CUDA import test because
WSL has no available CUDA device and three noncoherent-memory variants. The WSL
CUDA skip is expected and does not block this task.

Learning: Trace the owner of the Vulkan buffer, OS handle, CUDA mapping and session.
The next task must add GPU ordering before either API uses the shared allocation.
