# C++ and CUDA toolchain compatibility

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Probe one host compiler per OS, `std::expected` and GLM/Hpp header compatibility, a
plain-data CUDA20 adapter and a tiny NVCC producer. Run a CUDA-enabled binary
with no usable driver/device. Keep the producer as an example that can grow with
later Vulkan/CUDA features.

Non-goal: No shared allocation or frame scheduler.

Interface: Panda::Cuda facade, private C++20 plain-data adapter and application CUDA20 kernel boundary.

Paths: `cmake/`, `src/cuda/`, `../../../include/cuda/panda/cuda`,
`../../../examples/cuda_connection/`, `../../../tests/cases/unit/cuda/`,
`../../../tests/cases/integration/cuda/`, `../../../tests/support/mocks/external/cuda/` and
directly affected documentation.
Specifications: [Package](../../design/package-and-toolchain.md#c23-and-cuda20-with-one-host-toolchain), [compute availability](../../design/contracts/compute.md#interoperability).

## Acceptance

Record exact compiler, SDK, toolkit and runtime linkage on Windows/Linux; build/link/run or report unsupported. Resolve a failed probe in the package specification before GPU expansion.

Learning: Show why the CUDA translation unit cannot include the C++23 engine facade.

## Current evidence

Windows x64 Debug and Release: CMake 4.4.2, Ninja 1.13.2, MSVC 19.51.36260,
CUDA Toolkit/NVCC 13.4.92 and Windows SDK 10.0.26100.0. The example configured
with `PANDA_BUILD_CUDA=ON` and `PANDA_BUILD_EXAMPLES=ON`, built and ran directly;
both executions printed `CUDA producer returned 42`. The actual compile
commands use `/std:c++latest` for the facade, `/std:c++20` for both host adapters,
and `-std=c++20 -ccbin=<same MSVC>` for `Kernel.cu`. `dumpbin /dependents`
shows no cudart or NVIDIA driver DLL dependency in either executable; MSVC/UCRT
DLLs remain. The CUDA Runtime is linked statically. This process-dependency
result does not substitute for launching without an NVIDIA driver.
The example kernel uses `CUDA_ARCHITECTURES native`; the Windows build command
passes `-arch=native` to NVCC, with no Panda-chosen architecture number.

`PANDA_BUILD_CUDA=OFF` configured and built `simple_scene` without CUDA toolkit
discovery, CUDA language or CUDA tests. With CUDA on and examples off, `panda_cuda` built
using `FindCUDAToolkit` without enabling the CUDA language. The Windows quality
build of the new targets passed warnings-as-errors, clang-tidy and formatting.
Temporary C-style casts in `src/cuda/CudaAdapter.cpp` and `CudaAdapter.hpp`
were both rejected as errors by the actual quality build, then removed;
cppcheck was unavailable locally. The example is not registered as a CTest case.
With `PANDA_BUILD_CUDA=ON`, the full Windows build passed 45 noninteractive
tests, including six adapter unit cases and one real Runtime integration case,
plus 22 formatting checks. The integration case passed on a machine with a
usable device. With `CUDA_VISIBLE_DEVICES=-1`, the example exited successfully
with `CUDA device unavailable` and the real Runtime integration case passed.

A Windows CLion configuration using MSYS2 UCRT64 `g++.exe` as NVCC's host
failed CUDA compiler detection with `nvcc fatal: Host compiler targets
unsupported OS.` The CUDA module and example do not override this NVIDIA
diagnostic or pin a toolkit version in CMake. The Windows CUDA run above used
MSVC; the GCC/CUDA combination is not a passing probe.

The Hpp check is separate from the CUDA example. A Windows MSVC 19.51 C++23
translation unit including `panda/Panda.hpp`, GLM 1.0.3 and ordinary
`vulkan/vulkan.hpp` from Vulkan SDK 1.4.341.1 compiled. Its `glm::vec3` and
`glm::mat4` packed size/alignment assertions and `std::expected` assertion
passed. No current Panda source includes Vulkan-Hpp; the
[package specification](../../design/package-and-toolchain.md#native-graphics-api-without-macro-leakage)
uses the default `vk` namespace.

Ubuntu 26.04 WSL2: CMake 4.2.3, GCC 16.0.1, CUDA Toolkit/NVCC 13.4.92,
Windows NVIDIA driver 617.14 and GeForce RTX 4070 Ti. The complete CUDA 13.4
toolkit is installed under `/usr/local/cuda`; the Linux GPU driver comes from
Windows. The C++23 facade, C++20 adapter and CUDA20 example built with GCC as
NVCC's host compiler. The kernel command uses `-std=c++20 -arch=native`.
All seven CUDA tests passed (six adapter unit cases and one real Runtime
integration case). The example printed `CUDA producer returned 42`, and with
`CUDA_VISIBLE_DEVICES=-1` it printed `CUDA device unavailable; Panda core remains
usable`; both returned success. `ldd` shows libstdc++, libm, libgcc_s and libc,
with no dynamic cudart or NVIDIA driver library. For the Linux no-driver probe,
`unshare --mount --propagation private` created a process-local mount namespace;
a tmpfs mount over `/usr/lib/wsl/lib` hid `libcuda.so.1` from the example without
altering the host. The example returned success and printed `CUDA driver
unavailable; Panda core remains usable`. The real Runtime integration binary
also passed in that namespace. A subsequent ordinary launch returned `CUDA
producer returned 42`, confirming the host driver remained available. WSL is
the Linux verification environment for this task. On [PR #50](https://github.com/a-soltysik/Panda/pull/50),
the hosted Linux/GCC and Windows/MSVC CUDA quality jobs both built with
clang-tidy and cppcheck and passed seven CUDA cases and 18 formatting checks.
The ordinary quality and test jobs passed too. A second run restored CUDA Toolkit
13.4.92 from the OS-specific cache on both systems and passed again. The hosted
Windows availability test passed without a GPU; it does not record the exact
unavailable reason. The agreed missing-driver proof is the isolated WSL run,
not a separate Windows driverless launch.

The application kernel includes only its plain-data C++20 header and CUDA
Runtime headers. This keeps NVCC away from the C++23 facade and its standard
library requirements while the same host compiler links the two sides.
