# CUDA connection example

`cuda_connection` checks CUDA availability, tries to import a Vulkan storage buffer
into CUDA, and runs a kernel that produces `42` in a separate `cudaMalloc` allocation.
The imported buffer is not used by the kernel: Panda does not yet expose a safe
cross-API handoff. If Vulkan external-buffer export or a matching Vulkan/CUDA device
is unavailable, the example reports that and continues with the standalone kernel.

The complete shared-buffer path requires a Vulkan 1.3 device with external-memory
export support as well as a matching CUDA device. The standalone kernel requires
only CUDA. Building requires CUDA Toolkit and a compatible host compiler. By default,
the build detects the local GPU architecture. To configure without a GPU, pass
`-DCMAKE_CUDA_ARCHITECTURES=89` or another explicit architecture. Running the kernel needs an
NVIDIA GPU. Missing CUDA drivers or devices are reported without failing the
application. CI builds and analyzes the example without running it.

The example adds a `StreamSink` to `std::clog` before querying CUDA. The logger
starts with no sinks, so applications choose where engine logs are written.

Run from the repository root.

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
