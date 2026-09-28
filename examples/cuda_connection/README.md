# CUDA connection example

This example builds Panda's optional CUDA module and an application-owned CUDA20
kernel with the same host compiler. The C++23 `main.cpp` calls
`panda::cuda::queryAvailability()`, then calls a C++20 producer through a plain-data
header. `Kernel.cu` includes neither Panda headers nor C++23 library facilities.
The kernel writes `42` to a device buffer; the producer reads it back solely to
check this example. It does not share a Vulkan resource with CUDA yet.

Configure and run with a supported host compiler and CUDA Toolkit installed:

```sh
cmake --preset msvc-tests -B build-cuda-example -DPANDA_BUILD_CUDA=ON -DPANDA_BUILD_EXAMPLES=ON
cmake --build build-cuda-example --target cuda_connection
./build-cuda-example/examples/cuda_connection/cuda_connection.exe
```

On Linux, replace `msvc-tests` with `gcc-tests` and omit `.exe` from the final
command. Use a host compiler supported by the installed toolkit; the
[toolchain task](../../docs/development/tasks/toolchain-compatibility.md) records
which combinations have run. The example's CUDA target uses C++20 and lets CMake/NVCC
detect the build machine's GPU architecture with `CUDA_ARCHITECTURES native`.
Build the binary on a machine with a supported GPU so NVCC can detect its
architecture before running the unavailable-driver scenario. If the Runtime
reports no usable driver or device, the example prints that outcome and exits
successfully; other initialization or producer errors fail the run.

On WSL, the no-driver path can be exercised without changing the host driver.
From a Linux shell in the repository, run the built example in a separate mount
namespace with WSL's driver library directory hidden for that process:

```sh
sudo unshare --mount --propagation private sh -c \
  'mount -t tmpfs -o size=1m tmpfs /usr/lib/wsl/lib &&
   test ! -e /usr/lib/wsl/lib/libcuda.so.1 && exec "$1"' \
  sh "$(realpath build-cuda-example/examples/cuda_connection/cuda_connection)"
```

It should print `CUDA driver unavailable; Panda core remains usable` and return
success. An ordinary launch afterward should still use the GPU.

The availability query may initialize the CUDA Runtime and block. A successful
query is only a current capability check, not a device match with Vulkan or a
promise that later GPU work will succeed. Future integration can expand this
example when Panda has Vulkan resources and CUDA sharing.
