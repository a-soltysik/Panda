#include <cuda_runtime_api.h>
#include <driver_types.h>

#include "CudaRuntimeMock.hpp"
#include "ScopedMock.hpp"

extern "C" {

auto CUDARTAPI cudaGetDeviceCount(int* count) -> cudaError_t
{
    return panda::test::ScopedMock<panda::test::CudaRuntimeMock>::getActiveMock().cudaGetDeviceCount(count);
}
}
