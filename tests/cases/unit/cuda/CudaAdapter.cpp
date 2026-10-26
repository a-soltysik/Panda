#include "CudaAdapter.hpp"

#include <driver_types.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

#include "ScopedMock.hpp"
#include "external/cuda/CudaRuntimeMock.hpp"

namespace
{

using panda::cuda::detail::NativeAvailability;
using panda::cuda::detail::NativeInteropStatus;

template <typename Handle>
auto fakeHandle(std::max_align_t& token) -> Handle
{
    // The opaque CUDA handle is only compared and passed to a mock, never dereferenced.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<Handle>(&token);
}

class CudaAdapterTest : public testing::Test
{
protected:
    panda::test::ScopedMock<panda::test::CudaRuntimeMock> cudaRuntime;
};

}

TEST_F(CudaAdapterTest, ReportsAvailableDevice)
{
    EXPECT_CALL(cudaRuntime, cudaGetDeviceCount(testing::NotNull()))
        .WillOnce(testing::DoAll(testing::SetArgPointee<0>(1), testing::Return(cudaSuccess)));

    const auto result = panda::cuda::detail::queryNativeAvailability();
    EXPECT_EQ(result.availability, NativeAvailability::Available);
    EXPECT_EQ(result.nativeCode, 0);
}

TEST_F(CudaAdapterTest, ReportsRuntimeNoDevice)
{
    EXPECT_CALL(cudaRuntime, cudaGetDeviceCount(testing::NotNull())).WillOnce(testing::Return(cudaErrorNoDevice));

    const auto result = panda::cuda::detail::queryNativeAvailability();
    EXPECT_EQ(result.availability, NativeAvailability::NoDevice);
    EXPECT_EQ(result.nativeCode, cudaErrorNoDevice);
}

TEST_F(CudaAdapterTest, ReportsZeroDevices)
{
    EXPECT_CALL(cudaRuntime, cudaGetDeviceCount(testing::NotNull()))
        .WillOnce(testing::DoAll(testing::SetArgPointee<0>(0), testing::Return(cudaSuccess)));

    const auto result = panda::cuda::detail::queryNativeAvailability();
    EXPECT_EQ(result.availability, NativeAvailability::NoDevice);
    EXPECT_EQ(result.nativeCode, 0);
}

TEST_F(CudaAdapterTest, ReportsInsufficientDriver)
{
    EXPECT_CALL(cudaRuntime, cudaGetDeviceCount(testing::NotNull()))
        .WillOnce(testing::Return(cudaErrorInsufficientDriver));

    const auto result = panda::cuda::detail::queryNativeAvailability();
    EXPECT_EQ(result.availability, NativeAvailability::NoDriver);
    EXPECT_EQ(result.nativeCode, cudaErrorInsufficientDriver);
}

TEST_F(CudaAdapterTest, ReportsSystemDriverMismatch)
{
    EXPECT_CALL(cudaRuntime, cudaGetDeviceCount(testing::NotNull()))
        .WillOnce(testing::Return(cudaErrorSystemDriverMismatch));

    const auto result = panda::cuda::detail::queryNativeAvailability();
    EXPECT_EQ(result.availability, NativeAvailability::NoDriver);
    EXPECT_EQ(result.nativeCode, cudaErrorSystemDriverMismatch);
}

TEST_F(CudaAdapterTest, PreservesUnexpectedRuntimeFailure)
{
    EXPECT_CALL(cudaRuntime, cudaGetDeviceCount(testing::NotNull()))
        .WillOnce(testing::Return(cudaErrorInitializationError));

    const auto result = panda::cuda::detail::queryNativeAvailability();
    EXPECT_EQ(result.availability, NativeAvailability::Failure);
    EXPECT_EQ(result.nativeCode, cudaErrorInitializationError);
}

TEST_F(CudaAdapterTest, MatchesUuidBeforeCreatingSession)
{
    const auto uuid =
        std::array<std::uint8_t,
                   16> {0x85, 0x1d, 0x8e, 0x83, 0x2b, 0xf3, 0xf1, 0xd8, 0xda, 0xc6, 0x36, 0xe2, 0x78, 0xaf, 0xbd, 0xcb};
    auto firstProperties = cudaDeviceProp {};
    firstProperties.uuid.bytes[0] = 9;
    auto matchingProperties = cudaDeviceProp {};
    std::memcpy(std::span {matchingProperties.uuid.bytes}.data(), uuid.data(), uuid.size());
    std::max_align_t streamToken {};
    auto* const stream = fakeHandle<cudaStream_t>(streamToken);

    EXPECT_CALL(cudaRuntime, cudaGetDeviceCount(testing::NotNull()))
        .WillOnce(testing::DoAll(testing::SetArgPointee<0>(2), testing::Return(cudaSuccess)));
    EXPECT_CALL(cudaRuntime, cudaGetDeviceProperties(testing::NotNull(), 0))
        .WillOnce(testing::DoAll(testing::SetArgPointee<0>(firstProperties), testing::Return(cudaSuccess)));
    EXPECT_CALL(cudaRuntime, cudaGetDeviceProperties(testing::NotNull(), 1))
        .WillOnce(testing::DoAll(testing::SetArgPointee<0>(matchingProperties), testing::Return(cudaSuccess)));
    EXPECT_CALL(cudaRuntime, cudaSetDevice(1)).Times(2).WillRepeatedly(testing::Return(cudaSuccess));
    EXPECT_CALL(cudaRuntime, cudaStreamCreateWithFlags(testing::NotNull(), cudaStreamNonBlocking))
        .WillOnce(testing::DoAll(testing::SetArgPointee<0>(stream), testing::Return(cudaSuccess)));
    EXPECT_CALL(cudaRuntime, cudaStreamDestroy(stream)).WillOnce(testing::Return(cudaSuccess));

    const auto created = panda::cuda::detail::createInteropSession(uuid.data());
    ASSERT_EQ(created.status, NativeInteropStatus::Success);
    ASSERT_NE(created.session, nullptr);
    EXPECT_EQ(panda::cuda::detail::destroyInteropSession(created.session), cudaSuccess);
}

TEST_F(CudaAdapterTest, RejectsWhenNoCudaDeviceMatchesUuid)
{
    const auto uuid = std::array<std::uint8_t, 16> {1};
    const auto properties = cudaDeviceProp {};

    EXPECT_CALL(cudaRuntime, cudaGetDeviceCount(testing::NotNull()))
        .WillOnce(testing::DoAll(testing::SetArgPointee<0>(1), testing::Return(cudaSuccess)));
    EXPECT_CALL(cudaRuntime, cudaGetDeviceProperties(testing::NotNull(), 0))
        .WillOnce(testing::DoAll(testing::SetArgPointee<0>(properties), testing::Return(cudaSuccess)));

    const auto result = panda::cuda::detail::createInteropSession(uuid.data());
    EXPECT_EQ(result.status, NativeInteropStatus::Unsupported);
    EXPECT_EQ(result.session, nullptr);
}

TEST_F(CudaAdapterTest, ImportsDedicatedExternalAllocationAndMapsLogicalRange)
{
    const auto uuid = std::array<std::uint8_t, 16> {1, 2, 3};
    auto properties = cudaDeviceProp {};
    const auto deviceUuid = std::span {properties.uuid.bytes};
    std::ranges::copy(uuid, deviceUuid.begin());
    std::max_align_t streamToken {};
    std::max_align_t memoryToken {};
    std::max_align_t pointerToken {};
    auto* const stream = fakeHandle<cudaStream_t>(streamToken);
    auto* const externalMemory = fakeHandle<cudaExternalMemory_t>(memoryToken);
    void* devicePointer = &pointerToken;
#ifdef _WIN32
    constexpr auto handle = panda::cuda::detail::NativeMemoryHandleType::OpaqueWin32;
    constexpr auto nativeHandleType = cudaExternalMemoryHandleTypeOpaqueWin32;
#else
    constexpr auto handle = panda::cuda::detail::NativeMemoryHandleType::OpaqueFd;
    constexpr auto nativeHandleType = cudaExternalMemoryHandleTypeOpaqueFd;
#endif

    EXPECT_CALL(cudaRuntime, cudaGetDeviceCount(testing::NotNull()))
        .WillOnce(testing::DoAll(testing::SetArgPointee<0>(1), testing::Return(cudaSuccess)));
    EXPECT_CALL(cudaRuntime, cudaGetDeviceProperties(testing::NotNull(), 0))
        .WillOnce(testing::DoAll(testing::SetArgPointee<0>(properties), testing::Return(cudaSuccess)));
    EXPECT_CALL(cudaRuntime, cudaSetDevice(0)).Times(4).WillRepeatedly(testing::Return(cudaSuccess));
    EXPECT_CALL(cudaRuntime, cudaStreamCreateWithFlags(testing::NotNull(), cudaStreamNonBlocking))
        .WillOnce(testing::DoAll(testing::SetArgPointee<0>(stream), testing::Return(cudaSuccess)));
    EXPECT_CALL(cudaRuntime, cudaImportExternalMemory(testing::NotNull(), testing::NotNull()))
        .WillOnce([&](auto* output, const auto* descriptor) {
            EXPECT_EQ(descriptor->type, nativeHandleType);
            EXPECT_EQ(descriptor->size, 4096);
            EXPECT_EQ(descriptor->flags, cudaExternalMemoryDedicated);
            *output = externalMemory;
            return cudaSuccess;
        });
    EXPECT_CALL(cudaRuntime, cudaExternalMemoryGetMappedBuffer(testing::NotNull(), externalMemory, testing::NotNull()))
        .WillOnce([&](void** output, auto, const auto* descriptor) {
            EXPECT_EQ(descriptor->size, 1024);
            *output = devicePointer;
            return cudaSuccess;
        });
    EXPECT_CALL(cudaRuntime, cudaFree(devicePointer)).WillOnce(testing::Return(cudaSuccess));
    EXPECT_CALL(cudaRuntime, cudaDestroyExternalMemory(externalMemory)).WillOnce(testing::Return(cudaSuccess));
    EXPECT_CALL(cudaRuntime, cudaStreamDestroy(stream)).WillOnce(testing::Return(cudaSuccess));

    const auto created = panda::cuda::detail::createInteropSession(uuid.data());
    ASSERT_EQ(created.status, NativeInteropStatus::Success);
    const auto imported = panda::cuda::detail::importExternalBuffer(
        created.session,
        {.handle = 7, .handleType = handle, .allocationSize = 4096, .bufferSize = 1024});
    ASSERT_EQ(imported.status, NativeInteropStatus::Success);
    EXPECT_TRUE(imported.imported);
    EXPECT_EQ(imported.externalMemory, externalMemory);
    EXPECT_EQ(imported.devicePointer, devicePointer);
    EXPECT_EQ(panda::cuda::detail::destroyExternalBuffer(created.session, imported), cudaSuccess);
    EXPECT_EQ(panda::cuda::detail::destroyInteropSession(created.session), cudaSuccess);
}

TEST_F(CudaAdapterTest, ImportsOpaquePlatformHandleAsCudaTimelineSemaphore)
{
    const auto uuid = std::array<std::uint8_t, 16> {1, 2, 3};
    auto properties = cudaDeviceProp {};
    std::ranges::copy(uuid, std::span {properties.uuid.bytes}.begin());
    std::max_align_t streamToken {};
    std::max_align_t semaphoreToken {};
    auto* const stream = fakeHandle<cudaStream_t>(streamToken);
    auto* const semaphore = fakeHandle<cudaExternalSemaphore_t>(semaphoreToken);
#ifdef _WIN32
    constexpr auto handleType = panda::cuda::detail::NativeSemaphoreHandleType::OpaqueWin32;
    constexpr auto cudaHandleType = cudaExternalSemaphoreHandleTypeTimelineSemaphoreWin32;
#else
    constexpr auto handleType = panda::cuda::detail::NativeSemaphoreHandleType::OpaqueFd;
    constexpr auto cudaHandleType = cudaExternalSemaphoreHandleTypeTimelineSemaphoreFd;
#endif
    constexpr auto handle = std::intptr_t {7};

    EXPECT_CALL(cudaRuntime, cudaGetDeviceCount(testing::NotNull()))
        .WillOnce(testing::DoAll(testing::SetArgPointee<0>(1), testing::Return(cudaSuccess)));
    EXPECT_CALL(cudaRuntime, cudaGetDeviceProperties(testing::NotNull(), 0))
        .WillOnce(testing::DoAll(testing::SetArgPointee<0>(properties), testing::Return(cudaSuccess)));
    EXPECT_CALL(cudaRuntime, cudaSetDevice(0)).Times(4).WillRepeatedly(testing::Return(cudaSuccess));
    EXPECT_CALL(cudaRuntime, cudaStreamCreateWithFlags(testing::NotNull(), cudaStreamNonBlocking))
        .WillOnce(testing::DoAll(testing::SetArgPointee<0>(stream), testing::Return(cudaSuccess)));
    EXPECT_CALL(cudaRuntime, cudaImportExternalSemaphore(testing::NotNull(), testing::NotNull()))
        .WillOnce([&](auto* output, const auto* descriptor) {
            EXPECT_EQ(descriptor->type, cudaHandleType);
            EXPECT_EQ(descriptor->flags, 0U);
#ifdef _WIN32
            // The opaque HANDLE is passed through as intptr_t by Core.
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast, performance-no-int-to-ptr)
            EXPECT_EQ(descriptor->handle.win32.handle, reinterpret_cast<void*>(handle));
#else
            EXPECT_EQ(descriptor->handle.fd, static_cast<int>(handle));
#endif
            *output = semaphore;
            return cudaSuccess;
        });
    EXPECT_CALL(cudaRuntime, cudaDestroyExternalSemaphore(semaphore)).WillOnce(testing::Return(cudaSuccess));
    EXPECT_CALL(cudaRuntime, cudaStreamDestroy(stream)).WillOnce(testing::Return(cudaSuccess));

    const auto created = panda::cuda::detail::createInteropSession(uuid.data());
    ASSERT_EQ(created.status, NativeInteropStatus::Success);
    const auto imported =
        panda::cuda::detail::importExternalSemaphore(created.session, {.handle = handle, .handleType = handleType});
    ASSERT_EQ(imported.status, NativeInteropStatus::Success);
    EXPECT_TRUE(imported.imported);
    EXPECT_EQ(imported.externalSemaphore, semaphore);
    EXPECT_EQ(panda::cuda::detail::destroyExternalSemaphore(created.session, imported), cudaSuccess);
    EXPECT_EQ(panda::cuda::detail::destroyInteropSession(created.session), cudaSuccess);
}
