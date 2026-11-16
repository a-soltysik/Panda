#include <driver_types.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstring>
#include <panda/DeviceUuid.hpp>
#include <panda/cuda/CudaSession.hpp>
#include <span>

#include "FakeHandle.hpp"
#include "ScopedMock.hpp"
#include "external/cuda/CudaRuntimeMock.hpp"

namespace
{
using panda::test::fakeHandle;
}

TEST(CudaSession, CreatesAndClosesFromTemporaryUuidWithoutEngine)
{
    auto cudaRuntime = panda::test::ScopedMock<panda::test::CudaRuntimeMock> {};
    static constexpr auto uuid = panda::DeviceUuid {1, 2, 3};
    auto properties = cudaDeviceProp {};
    std::memcpy(std::span {properties.uuid.bytes}.data(), uuid.data(), uuid.size());
    auto* const stream = fakeHandle<cudaStream_t>(1);

    EXPECT_CALL(cudaRuntime, cudaGetDeviceCount(testing::NotNull()))
        .WillOnce(testing::DoAll(testing::SetArgPointee<0>(1), testing::Return(cudaSuccess)));
    EXPECT_CALL(cudaRuntime, cudaGetDeviceProperties(testing::NotNull(), 0))
        .WillOnce(testing::DoAll(testing::SetArgPointee<0>(properties), testing::Return(cudaSuccess)));
    EXPECT_CALL(cudaRuntime, cudaSetDevice(0)).Times(2).WillRepeatedly(testing::Return(cudaSuccess));
    EXPECT_CALL(cudaRuntime, cudaStreamCreateWithFlags(testing::NotNull(), cudaStreamNonBlocking))
        .WillOnce(testing::DoAll(testing::SetArgPointee<0>(stream), testing::Return(cudaSuccess)));
    EXPECT_CALL(cudaRuntime, cudaStreamDestroy(stream)).WillOnce(testing::Return(cudaSuccess));

    auto session = panda::cuda::CudaSession::create(panda::DeviceUuid {1, 2, 3});
    ASSERT_TRUE(session) << session.error().message;
    EXPECT_TRUE(session->close());
}
