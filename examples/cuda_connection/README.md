# CUDA connection example

`cuda_connection` checks CUDA availability, imports a Vulkan storage buffer into
CUDA, then runs three passes on the same buffer. Each pass uploads the next value
with Vulkan, queues a clear on Panda's session stream between the paired timeline
semaphore wait and signal, and reacquires Vulkan ownership. It also runs a separate
kernel that produces `42` in a `cudaMalloc` allocation. The shared-buffer path
requires external-memory and timeline-semaphore export and a matching Vulkan/CUDA
device; a missing capability aborts with context instead of continuing.

The shared-buffer path requires a Vulkan 1.3 device with external-memory and
timeline-semaphore export support as well as a matching CUDA device. The standalone
kernel requires only CUDA. The example demonstrates enqueue and ownership ordering;
it does not read the cleared bytes back or prove GPU-side data correctness. Building
requires CUDA Toolkit and a compatible host compiler. By default,
the build detects the local GPU architecture. To configure without a GPU, pass
`-DCMAKE_CUDA_ARCHITECTURES=89` or another explicit architecture. Running the kernel needs an
NVIDIA GPU. Missing CUDA drivers or devices are reported without failing the
application. CI builds and analyzes the example without running it.

The example adds a `StreamSink` to `std::clog` before querying CUDA. The logger
starts with no sinks, so applications choose where engine logs are written.

Run from the repository root.

Essential operations use `panda::expect`: a failed operation aborts with context,
including in Release builds. Normal shutdown uses RAII; abort does not unwind
local objects. See [error handling](../error_handling/README.md) for recovery,
error propagation and detailed diagnostics.

Windows (MSVC):

```powershell
cmake --preset msvc-development -B build-cuda-example -DPANDA_BUILD_CUDA=ON
cmake --build build-cuda-example --target cuda_connection
./build-cuda-example/examples/cuda_connection/cuda_connection.exe
```

Linux (GCC):

```sh
cmake --preset gcc-development -B build-cuda-example -DPANDA_BUILD_CUDA=ON
cmake --build build-cuda-example --target cuda_connection
./build-cuda-example/examples/cuda_connection/cuda_connection
```

See the
[toolchain compatibility notes](../../docs/development/tasks/toolchain-compatibility.md)
for verified compiler and toolkit combinations.
