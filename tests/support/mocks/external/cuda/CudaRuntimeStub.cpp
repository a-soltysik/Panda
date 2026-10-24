#include <cuda_runtime_api.h>
#include <driver_types.h>

#include "CudaRuntimeMock.hpp"
#include "ScopedMock.hpp"

extern "C" {

auto CUDARTAPI cudaGetDeviceCount(int* count) -> cudaError_t
{
    return panda::test::ScopedMock<panda::test::CudaRuntimeMock>::getActiveMock().cudaGetDeviceCount(count);
}

auto CUDARTAPI cudaGetDeviceProperties(cudaDeviceProp* properties, int device) -> cudaError_t
{
    return panda::test::ScopedMock<panda::test::CudaRuntimeMock>::getActiveMock().cudaGetDeviceProperties(properties,
                                                                                                          device);
}

auto CUDARTAPI cudaSetDevice(int device) -> cudaError_t
{
    return panda::test::ScopedMock<panda::test::CudaRuntimeMock>::getActiveMock().cudaSetDevice(device);
}

auto CUDARTAPI cudaStreamCreateWithFlags(cudaStream_t* stream, unsigned int flags) -> cudaError_t
{
    return panda::test::ScopedMock<panda::test::CudaRuntimeMock>::getActiveMock().cudaStreamCreateWithFlags(stream,
                                                                                                            flags);
}

auto CUDARTAPI cudaStreamDestroy(cudaStream_t stream) -> cudaError_t
{
    return panda::test::ScopedMock<panda::test::CudaRuntimeMock>::getActiveMock().cudaStreamDestroy(stream);
}

// NOLINTNEXTLINE(readability-identifier-naming): The parameter name must match CUDA's C declaration.
auto CUDARTAPI cudaImportExternalMemory(cudaExternalMemory_t* extMem_out,
                                        const cudaExternalMemoryHandleDesc* memHandleDesc) -> cudaError_t
{
    return panda::test::ScopedMock<panda::test::CudaRuntimeMock>::getActiveMock().cudaImportExternalMemory(
        extMem_out,
        memHandleDesc);
}

auto CUDARTAPI cudaExternalMemoryGetMappedBuffer(void** devPtr,
                                                 cudaExternalMemory_t extMem,
                                                 const cudaExternalMemoryBufferDesc* bufferDesc) -> cudaError_t
{
    return panda::test::ScopedMock<panda::test::CudaRuntimeMock>::getActiveMock().cudaExternalMemoryGetMappedBuffer(
        devPtr,
        extMem,
        bufferDesc);
}

auto CUDARTAPI cudaFree(void* devPtr) -> cudaError_t
{
    return panda::test::ScopedMock<panda::test::CudaRuntimeMock>::getActiveMock().cudaFree(devPtr);
}

auto CUDARTAPI cudaDestroyExternalMemory(cudaExternalMemory_t extMem) -> cudaError_t
{
    return panda::test::ScopedMock<panda::test::CudaRuntimeMock>::getActiveMock().cudaDestroyExternalMemory(extMem);
}
}
