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
#include <utility>

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

    auto created = panda::cuda::detail::createInteropSession(uuid);
    ASSERT_EQ(created.status, NativeInteropStatus::Success);
    ASSERT_NE(created.session, nullptr);
    EXPECT_EQ(panda::cuda::detail::destroyInteropSession(std::move(created.session)), cudaSuccess);
}

TEST_F(CudaAdapterTest, RejectsWhenNoCudaDeviceMatchesUuid)
{
    const auto uuid = std::array<std::uint8_t, 16> {1};
    const auto properties = cudaDeviceProp {};

    EXPECT_CALL(cudaRuntime, cudaGetDeviceCount(testing::NotNull()))
        .WillOnce(testing::DoAll(testing::SetArgPointee<0>(1), testing::Return(cudaSuccess)));
    EXPECT_CALL(cudaRuntime, cudaGetDeviceProperties(testing::NotNull(), 0))
        .WillOnce(testing::DoAll(testing::SetArgPointee<0>(properties), testing::Return(cudaSuccess)));

    const auto result = panda::cuda::detail::createInteropSession(uuid);
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
    auto* const cudaMemory = fakeHandle<cudaExternalMemory_t>(memoryToken);
    auto* const externalMemory = cudaMemory;
    const auto pointerBytes = std::as_writable_bytes(std::span {&pointerToken, std::size_t {1}});
    auto* const devicePointer = pointerBytes.data();
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
            *output = cudaMemory;
            return cudaSuccess;
        });
    EXPECT_CALL(cudaRuntime, cudaExternalMemoryGetMappedBuffer(testing::NotNull(), cudaMemory, testing::NotNull()))
        .WillOnce([&](auto* output, auto, const auto* descriptor) {
            EXPECT_EQ(descriptor->size, 1024);
            *output = devicePointer;
            return cudaSuccess;
        });
    EXPECT_CALL(cudaRuntime, cudaFree(devicePointer)).WillOnce(testing::Return(cudaSuccess));
    EXPECT_CALL(cudaRuntime, cudaDestroyExternalMemory(cudaMemory)).WillOnce(testing::Return(cudaSuccess));
    EXPECT_CALL(cudaRuntime, cudaStreamDestroy(stream)).WillOnce(testing::Return(cudaSuccess));

    auto created = panda::cuda::detail::createInteropSession(uuid);
    ASSERT_EQ(created.status, NativeInteropStatus::Success);
    ASSERT_NE(created.session, nullptr);
    const auto imported = panda::cuda::detail::importExternalBuffer(
        *created.session,
        {.handle = 7, .handleType = handle, .allocationSize = 4096, .bufferSize = 1024});
    ASSERT_EQ(imported.status, NativeInteropStatus::Success);
    EXPECT_TRUE(imported.imported);
    EXPECT_EQ(imported.externalMemory, externalMemory);
    EXPECT_EQ(imported.devicePointer, devicePointer);
    EXPECT_EQ(panda::cuda::detail::destroyExternalBuffer(*created.session, imported), cudaSuccess);
    EXPECT_EQ(panda::cuda::detail::destroyInteropSession(std::move(created.session)), cudaSuccess);
}

TEST_F(CudaAdapterTest, ImportsOpaquePlatformHandleAsCudaTimelineSemaphore)
{
    const auto uuid = std::array<std::uint8_t, 16> {1, 2, 3};
    auto properties = cudaDeviceProp {};
    std::ranges::copy(uuid, std::span {properties.uuid.bytes}.begin());
    std::max_align_t streamToken {};
    std::max_align_t semaphoreToken {};
    auto* const stream = fakeHandle<cudaStream_t>(streamToken);
    auto* const cudaSemaphore = fakeHandle<cudaExternalSemaphore_t>(semaphoreToken);
    auto* const semaphore = cudaSemaphore;
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
            using NativeWin32Handle = decltype(std::declval<cudaExternalSemaphoreHandleDesc>().handle.win32.handle);
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast, performance-no-int-to-ptr): CUDA expects a Win32 HANDLE.
            EXPECT_EQ(descriptor->handle.win32.handle, reinterpret_cast<NativeWin32Handle>(handle));
#else
            EXPECT_EQ(descriptor->handle.fd, static_cast<int>(handle));
#endif
            *output = cudaSemaphore;
            return cudaSuccess;
        });
    EXPECT_CALL(cudaRuntime, cudaDestroyExternalSemaphore(cudaSemaphore)).WillOnce(testing::Return(cudaSuccess));
    EXPECT_CALL(cudaRuntime, cudaStreamDestroy(stream)).WillOnce(testing::Return(cudaSuccess));

    auto created = panda::cuda::detail::createInteropSession(uuid);
    ASSERT_EQ(created.status, NativeInteropStatus::Success);
    ASSERT_NE(created.session, nullptr);
    const auto imported =
        panda::cuda::detail::importExternalSemaphore(*created.session, {.handle = handle, .handleType = handleType});
    ASSERT_EQ(imported.status, NativeInteropStatus::Success);
    EXPECT_TRUE(imported.imported);
    EXPECT_EQ(imported.externalSemaphore, semaphore);
    EXPECT_EQ(panda::cuda::detail::destroyExternalSemaphore(*created.session, imported), cudaSuccess);
    EXPECT_EQ(panda::cuda::detail::destroyInteropSession(std::move(created.session)), cudaSuccess);
}

TEST_F(CudaAdapterTest, QueuesTimelineSemaphoreWaitAndSignalOnTheSessionStream)
{
    const auto uuid = std::array<std::uint8_t, 16> {1, 2, 3};
    auto properties = cudaDeviceProp {};
    std::ranges::copy(uuid, std::span {properties.uuid.bytes}.begin());
    std::max_align_t streamToken {};
    std::max_align_t semaphoreToken {};
    auto* const stream = fakeHandle<cudaStream_t>(streamToken);
    auto* const cudaSemaphore = fakeHandle<cudaExternalSemaphore_t>(semaphoreToken);
    auto* const semaphore = cudaSemaphore;

    EXPECT_CALL(cudaRuntime, cudaGetDeviceCount(testing::NotNull()))
        .WillOnce(testing::DoAll(testing::SetArgPointee<0>(1), testing::Return(cudaSuccess)));
    EXPECT_CALL(cudaRuntime, cudaGetDeviceProperties(testing::NotNull(), 0))
        .WillOnce(testing::DoAll(testing::SetArgPointee<0>(properties), testing::Return(cudaSuccess)));
    EXPECT_CALL(cudaRuntime, cudaSetDevice(0)).Times(5).WillRepeatedly(testing::Return(cudaSuccess));
    EXPECT_CALL(cudaRuntime, cudaStreamCreateWithFlags(testing::NotNull(), cudaStreamNonBlocking))
        .WillOnce(testing::DoAll(testing::SetArgPointee<0>(stream), testing::Return(cudaSuccess)));
    {
        const auto orderedCalls = testing::InSequence {};
        EXPECT_CALL(cudaRuntime, cudaWaitExternalSemaphoresAsync(testing::NotNull(), testing::NotNull(), 1, stream))
            .WillOnce([&](const auto* semaphores, const auto* params, auto, auto) {
                EXPECT_EQ(*semaphores, cudaSemaphore);
                EXPECT_EQ(params->params.fence.value, 11);
                return cudaSuccess;
            });
        EXPECT_CALL(cudaRuntime, cudaSignalExternalSemaphoresAsync(testing::NotNull(), testing::NotNull(), 1, stream))
            .WillOnce([&](const auto* semaphores, const auto* params, auto, auto) {
                EXPECT_EQ(*semaphores, cudaSemaphore);
                EXPECT_EQ(params->params.fence.value, 12);
                return cudaErrorNotSupported;
            });
        EXPECT_CALL(cudaRuntime, cudaStreamSynchronize(stream)).WillOnce(testing::Return(cudaSuccess));
        EXPECT_CALL(cudaRuntime, cudaStreamDestroy(stream)).WillOnce(testing::Return(cudaSuccess));
    }

    auto created = panda::cuda::detail::createInteropSession(uuid);
    ASSERT_EQ(created.status, NativeInteropStatus::Success);
    ASSERT_NE(created.session, nullptr);
    auto* const emptySemaphore = panda::cuda::detail::NativeExternalSemaphore {};
    const auto invalidWait = panda::cuda::detail::enqueueExternalSemaphoreWait(*created.session, semaphore, 0);
    EXPECT_EQ(invalidWait.status, NativeInteropStatus::Failure);
    EXPECT_EQ(invalidWait.nativeCode, cudaErrorInvalidValue);
    const auto missingSemaphore =
        panda::cuda::detail::enqueueExternalSemaphoreWait(*created.session, emptySemaphore, 11);
    EXPECT_EQ(missingSemaphore.nativeCode, cudaErrorInvalidValue);
    const auto missingSignal =
        panda::cuda::detail::enqueueExternalSemaphoreSignal(*created.session, emptySemaphore, 12);
    EXPECT_EQ(missingSignal.nativeCode, cudaErrorInvalidValue);
    const auto enqueuedWait = panda::cuda::detail::enqueueExternalSemaphoreWait(*created.session, semaphore, 11);
    EXPECT_EQ(enqueuedWait.status, NativeInteropStatus::Success);
    EXPECT_EQ(enqueuedWait.nativeCode, cudaSuccess);
    const auto enqueuedSignal = panda::cuda::detail::enqueueExternalSemaphoreSignal(*created.session, semaphore, 12);
    EXPECT_EQ(enqueuedSignal.status, NativeInteropStatus::Unsupported);
    EXPECT_EQ(enqueuedSignal.nativeCode, cudaErrorNotSupported);
    EXPECT_EQ(panda::cuda::detail::synchronizeInteropSession(*created.session), cudaSuccess);
    EXPECT_EQ(panda::cuda::detail::destroyInteropSession(std::move(created.session)), cudaSuccess);
}
