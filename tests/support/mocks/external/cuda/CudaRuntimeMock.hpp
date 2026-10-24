#pragma once

#include <driver_types.h>
#include <gmock/gmock.h>

namespace panda::test
{

class CudaRuntimeMock
{
public:
    // gMock's declaration macro only accepts leading return types.
    // NOLINTBEGIN(modernize-use-trailing-return-type)
    MOCK_METHOD(cudaError_t, cudaGetDeviceCount, (int* count));
    MOCK_METHOD(cudaError_t, cudaGetDeviceProperties, (cudaDeviceProp * properties, int device));
    MOCK_METHOD(cudaError_t, cudaSetDevice, (int device));
    MOCK_METHOD(cudaError_t, cudaStreamCreateWithFlags, (cudaStream_t * stream, unsigned int flags));
    MOCK_METHOD(cudaError_t, cudaStreamDestroy, (cudaStream_t stream));
    MOCK_METHOD(cudaError_t,
                cudaImportExternalMemory,
                (cudaExternalMemory_t * memory, const cudaExternalMemoryHandleDesc* descriptor));
    MOCK_METHOD(cudaError_t,
                cudaExternalMemoryGetMappedBuffer,
                (void** pointer, cudaExternalMemory_t memory, const cudaExternalMemoryBufferDesc* descriptor));
    MOCK_METHOD(cudaError_t, cudaFree, (void* pointer));
    MOCK_METHOD(cudaError_t, cudaDestroyExternalMemory, (cudaExternalMemory_t memory));
    // NOLINTEND(modernize-use-trailing-return-type)
};

}
