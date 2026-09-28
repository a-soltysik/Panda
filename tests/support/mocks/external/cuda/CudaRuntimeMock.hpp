#pragma once

#include <driver_types.h>
#include <gmock/gmock.h>

namespace panda::test
{

class CudaRuntimeMock
{
public:
    // gMock requires the return type as a macro argument.
    // NOLINTNEXTLINE(modernize-use-trailing-return-type)
    MOCK_METHOD(cudaError_t, cudaGetDeviceCount, (int* count));
};

}
