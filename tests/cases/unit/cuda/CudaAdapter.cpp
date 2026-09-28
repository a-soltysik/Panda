#include "CudaAdapter.hpp"

#include <driver_types.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "ScopedMock.hpp"
#include "external/cuda/CudaRuntimeMock.hpp"

namespace
{

using panda::cuda::detail::NativeAvailability;

class CudaAdapterTest : public testing::Test
{
protected:
    panda::test::ScopedMock<panda::test::CudaRuntimeMock> _cudaRuntime;
};

}

TEST_F(CudaAdapterTest, ReportsAvailableDevice)
{
    EXPECT_CALL(_cudaRuntime, cudaGetDeviceCount(testing::NotNull()))
        .WillOnce(testing::DoAll(testing::SetArgPointee<0>(1), testing::Return(cudaSuccess)));

    const auto result = panda::cuda::detail::queryNativeAvailability();
    EXPECT_EQ(result.availability, NativeAvailability::Available);
    EXPECT_EQ(result.nativeCode, 0);
}

TEST_F(CudaAdapterTest, ReportsRuntimeNoDevice)
{
    EXPECT_CALL(_cudaRuntime, cudaGetDeviceCount(testing::NotNull())).WillOnce(testing::Return(cudaErrorNoDevice));

    const auto result = panda::cuda::detail::queryNativeAvailability();
    EXPECT_EQ(result.availability, NativeAvailability::NoDevice);
    EXPECT_EQ(result.nativeCode, cudaErrorNoDevice);
}

TEST_F(CudaAdapterTest, ReportsZeroDevices)
{
    EXPECT_CALL(_cudaRuntime, cudaGetDeviceCount(testing::NotNull()))
        .WillOnce(testing::DoAll(testing::SetArgPointee<0>(0), testing::Return(cudaSuccess)));

    const auto result = panda::cuda::detail::queryNativeAvailability();
    EXPECT_EQ(result.availability, NativeAvailability::NoDevice);
    EXPECT_EQ(result.nativeCode, 0);
}

TEST_F(CudaAdapterTest, ReportsInsufficientDriver)
{
    EXPECT_CALL(_cudaRuntime, cudaGetDeviceCount(testing::NotNull()))
        .WillOnce(testing::Return(cudaErrorInsufficientDriver));

    const auto result = panda::cuda::detail::queryNativeAvailability();
    EXPECT_EQ(result.availability, NativeAvailability::NoDriver);
    EXPECT_EQ(result.nativeCode, cudaErrorInsufficientDriver);
}

TEST_F(CudaAdapterTest, ReportsSystemDriverMismatch)
{
    EXPECT_CALL(_cudaRuntime, cudaGetDeviceCount(testing::NotNull()))
        .WillOnce(testing::Return(cudaErrorSystemDriverMismatch));

    const auto result = panda::cuda::detail::queryNativeAvailability();
    EXPECT_EQ(result.availability, NativeAvailability::NoDriver);
    EXPECT_EQ(result.nativeCode, cudaErrorSystemDriverMismatch);
}

TEST_F(CudaAdapterTest, PreservesUnexpectedRuntimeFailure)
{
    EXPECT_CALL(_cudaRuntime, cudaGetDeviceCount(testing::NotNull()))
        .WillOnce(testing::Return(cudaErrorInitializationError));

    const auto result = panda::cuda::detail::queryNativeAvailability();
    EXPECT_EQ(result.availability, NativeAvailability::Failure);
    EXPECT_EQ(result.nativeCode, cudaErrorInitializationError);
}
