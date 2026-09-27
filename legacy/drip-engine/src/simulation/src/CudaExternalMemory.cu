#include <cuda_runtime_api.h>
#include <driver_types.h>

#include <cstddef>
#include <drip/common/utils/Assert.hpp>

#include "CudaExternalMemory.cuh"

namespace drip::sim
{

CudaExternalMemory::CudaExternalMemory(Handle handle, size_t size)
    : _size(size)
{
    auto handleDesc = cudaExternalMemoryHandleDesc {};
#ifdef _WIN32
    handleDesc.type = cudaExternalMemoryHandleTypeOpaqueWin32;
    handleDesc.handle.win32.handle = handle;
#else
    handleDesc.type = cudaExternalMemoryHandleTypeOpaqueFd;
    handleDesc.handle.fd = handle;
#endif

    handleDesc.size = size;
    handleDesc.flags = 0;
    common::Expect(cudaImportExternalMemory(&_memoryHandle, &handleDesc),
                   cudaSuccess,
                   "Failed to import external memory");

    auto bufferDesc = cudaExternalMemoryBufferDesc {};
    bufferDesc.size = size;

    common::Expect(cudaExternalMemoryGetMappedBuffer(&_data, _memoryHandle, &bufferDesc),
                   cudaSuccess,
                   "Failed to map external memory buffer");
}

CudaExternalMemory::~CudaExternalMemory() noexcept
{
    common::ShouldBe(cudaFree(_data), cudaSuccess, "Failed to free mapped buffer");
    common::ShouldBe(cudaDestroyExternalMemory(_memoryHandle), cudaSuccess, "Failed to destroy external memory");
}

auto CudaExternalMemory::getData() const -> void*
{
    return _data;
}

auto CudaExternalMemory::getSize() const -> size_t
{
    return _size;
}

}
