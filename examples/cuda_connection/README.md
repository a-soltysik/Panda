# CUDA connection example

`cuda_connection` checks CUDA availability and runs a kernel that produces `42`.
Requires CUDA Toolkit, a compatible host compiler and an NVIDIA GPU to build
(the GPU architecture is detected automatically). Missing drivers or devices at
runtime are reported without failing the application.

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
