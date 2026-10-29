#include <driver_types.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <panda/Context.hpp>
#include <panda/Error.hpp>
#include <panda/cuda/CudaSession.hpp>
#include <panda/cuda/SharedCudaBuffer.hpp>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <vulkan/vulkan.hpp>

#include "ScopedMock.hpp"
#include "external/cuda/CudaRuntimeMock.hpp"

namespace
{
template <typename Handle>
auto fakeHandle(std::max_align_t& token) -> Handle
{
    // The opaque CUDA handle is only compared and passed to a mock, never dereferenced.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<Handle>(&token);
}
}

TEST(ExternalBufferCudaSystem, ReleasesImportedMemoryWhenSemaphoreImportFails)
{
    auto context =
        panda::Context::create({.externalBufferSupport = panda::ContextOptions::ExternalBufferSupport::Required});
    if (!context && context.error().code == panda::ErrorCode::Unsupported)
    {
        GTEST_SKIP() << context.error().message;
    }
    ASSERT_TRUE(context) << context.error().message;

    auto buffer = context->getExternalBufferManager().createBuffer(
        {.size = 1024, .usage = vk::BufferUsageFlagBits::eStorageBuffer});
    if (!buffer && buffer.error().code == panda::ErrorCode::Unsupported)
    {
        GTEST_SKIP() << buffer.error().message;
    }
    ASSERT_TRUE(buffer) << buffer.error().message;
    if (!buffer->hasSemaphore())
    {
        GTEST_SKIP() << "The selected Vulkan device cannot export a timeline semaphore";
    }
    const auto bufferSize = buffer->getSize();

    auto cudaRuntime = panda::test::ScopedMock<panda::test::CudaRuntimeMock> {};
    const auto deviceUuid = context->getDeviceInfo().deviceUuid;
    auto properties = cudaDeviceProp {};
    std::ranges::copy(deviceUuid, std::span {properties.uuid.bytes}.begin());
    std::max_align_t streamToken {};
    std::max_align_t memoryToken {};
    std::max_align_t pointerToken {};
    auto* const stream = fakeHandle<cudaStream_t>(streamToken);
    auto* const memory = fakeHandle<cudaExternalMemory_t>(memoryToken);
    const auto pointerBytes = std::as_writable_bytes(std::span {&pointerToken, std::size_t {1}});
    auto* const devicePointer = pointerBytes.data();
    const auto semaphoreHandleValue = buffer->getSemaphoreHandle().get();
#ifdef _WIN32
    constexpr auto memoryHandleType = cudaExternalMemoryHandleTypeOpaqueWin32;
    constexpr auto semaphoreHandleType = cudaExternalSemaphoreHandleTypeTimelineSemaphoreWin32;
#else
    constexpr auto memoryHandleType = cudaExternalMemoryHandleTypeOpaqueFd;
    constexpr auto semaphoreHandleType = cudaExternalSemaphoreHandleTypeTimelineSemaphoreFd;
#endif

    EXPECT_CALL(cudaRuntime, cudaGetDeviceCount(testing::NotNull()))
        .WillOnce(testing::DoAll(testing::SetArgPointee<0>(1), testing::Return(cudaSuccess)));
    EXPECT_CALL(cudaRuntime, cudaGetDeviceProperties(testing::NotNull(), 0))
        .WillOnce(testing::DoAll(testing::SetArgPointee<0>(properties), testing::Return(cudaSuccess)));
    EXPECT_CALL(cudaRuntime, cudaSetDevice(0)).Times(6).WillRepeatedly(testing::Return(cudaSuccess));
    EXPECT_CALL(cudaRuntime, cudaStreamCreateWithFlags(testing::NotNull(), cudaStreamNonBlocking))
        .WillOnce(testing::DoAll(testing::SetArgPointee<0>(stream), testing::Return(cudaSuccess)));
    {
        const auto orderedCalls = testing::InSequence {};
        EXPECT_CALL(cudaRuntime, cudaImportExternalMemory(testing::NotNull(), testing::NotNull()))
            .WillOnce([&](auto* output, const auto* descriptor) {
                EXPECT_EQ(descriptor->type, memoryHandleType);
                EXPECT_EQ(descriptor->flags, cudaExternalMemoryDedicated);
                *output = memory;
                return cudaSuccess;
            });
        EXPECT_CALL(cudaRuntime, cudaExternalMemoryGetMappedBuffer(testing::NotNull(), memory, testing::NotNull()))
            .WillOnce([&](auto* output, auto, const auto* descriptor) {
                EXPECT_EQ(descriptor->size, bufferSize);
                *output = devicePointer;
                return cudaSuccess;
            });
        EXPECT_CALL(cudaRuntime, cudaImportExternalSemaphore(testing::NotNull(), testing::NotNull()))
            .WillOnce([&](auto*, const auto* descriptor) {
                EXPECT_EQ(descriptor->type, semaphoreHandleType);
#ifdef _WIN32
                // The opaque HANDLE is passed through as intptr_t by Core.
                using NativeWin32Handle = decltype(std::declval<cudaExternalSemaphoreHandleDesc>().handle.win32.handle);
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast, performance-no-int-to-ptr): CUDA expects a Win32 HANDLE.
                EXPECT_EQ(descriptor->handle.win32.handle, reinterpret_cast<NativeWin32Handle>(semaphoreHandleValue));
#else
                EXPECT_EQ(descriptor->handle.fd, static_cast<int>(semaphoreHandleValue));
#endif
                return cudaErrorNotSupported;
            });
        EXPECT_CALL(cudaRuntime, cudaStreamSynchronize(stream)).WillOnce(testing::Return(cudaSuccess));
        EXPECT_CALL(cudaRuntime, cudaFree(devicePointer)).WillOnce(testing::Return(cudaSuccess));
        EXPECT_CALL(cudaRuntime, cudaDestroyExternalMemory(memory)).WillOnce(testing::Return(cudaSuccess));
    }
    EXPECT_CALL(cudaRuntime, cudaStreamDestroy(stream)).WillOnce(testing::Return(cudaSuccess));

    auto session = panda::cuda::CudaSession::create(*context);
    ASSERT_TRUE(session) << session.error().message;
    const auto imported = session->importBuffer(std::move(*buffer));
    ASSERT_FALSE(imported);
    EXPECT_EQ(imported.error().code, panda::ErrorCode::Unsupported);
    EXPECT_TRUE(session->close());
}

TEST(ExternalBufferCudaSystem, QueuesWorkBetweenTimelineWaitAndSignal)
{
    auto context =
        panda::Context::create({.externalBufferSupport = panda::ContextOptions::ExternalBufferSupport::Required});
    if (!context && context.error().code == panda::ErrorCode::Unsupported)
    {
        GTEST_SKIP() << context.error().message;
    }
    ASSERT_TRUE(context) << context.error().message;

    auto buffer = context->getExternalBufferManager().createBuffer(
        {.size = 1024, .usage = vk::BufferUsageFlagBits::eStorageBuffer});
    if (!buffer && buffer.error().code == panda::ErrorCode::Unsupported)
    {
        GTEST_SKIP() << buffer.error().message;
    }
    ASSERT_TRUE(buffer) << buffer.error().message;
    if (!buffer->hasSemaphore())
    {
        GTEST_SKIP() << "The selected Vulkan device cannot export a timeline semaphore";
    }

    auto cudaRuntime = panda::test::ScopedMock<panda::test::CudaRuntimeMock> {};
    const auto deviceUuid = context->getDeviceInfo().deviceUuid;
    auto properties = cudaDeviceProp {};
    std::ranges::copy(deviceUuid, std::span {properties.uuid.bytes}.begin());
    std::max_align_t streamToken {};
    std::max_align_t memoryToken {};
    std::max_align_t semaphoreToken {};
    std::max_align_t pointerToken {};
    auto* const stream = fakeHandle<cudaStream_t>(streamToken);
    auto* const memory = fakeHandle<cudaExternalMemory_t>(memoryToken);
    auto* const semaphore = fakeHandle<cudaExternalSemaphore_t>(semaphoreToken);
    const auto pointerBytes = std::as_writable_bytes(std::span {&pointerToken, std::size_t {1}});
    auto* const devicePointer = pointerBytes.data();
    auto callOrder = std::vector<int> {};

    EXPECT_CALL(cudaRuntime, cudaGetDeviceCount(testing::NotNull()))
        .WillOnce(testing::DoAll(testing::SetArgPointee<0>(1), testing::Return(cudaSuccess)));
    EXPECT_CALL(cudaRuntime, cudaGetDeviceProperties(testing::NotNull(), 0))
        .WillOnce(testing::DoAll(testing::SetArgPointee<0>(properties), testing::Return(cudaSuccess)));
    EXPECT_CALL(cudaRuntime, cudaSetDevice(0)).Times(16).WillRepeatedly(testing::Return(cudaSuccess));
    EXPECT_CALL(cudaRuntime, cudaStreamCreateWithFlags(testing::NotNull(), cudaStreamNonBlocking))
        .WillOnce(testing::DoAll(testing::SetArgPointee<0>(stream), testing::Return(cudaSuccess)));
    EXPECT_CALL(cudaRuntime, cudaImportExternalMemory(testing::NotNull(), testing::NotNull()))
        .WillOnce(testing::DoAll(testing::SetArgPointee<0>(memory), testing::Return(cudaSuccess)));
    EXPECT_CALL(cudaRuntime, cudaExternalMemoryGetMappedBuffer(testing::NotNull(), memory, testing::NotNull()))
        .WillOnce(testing::DoAll(testing::SetArgPointee<0>(devicePointer), testing::Return(cudaSuccess)));
    EXPECT_CALL(cudaRuntime, cudaImportExternalSemaphore(testing::NotNull(), testing::NotNull()))
        .WillOnce(testing::DoAll(testing::SetArgPointee<0>(semaphore), testing::Return(cudaSuccess)));
    EXPECT_CALL(cudaRuntime, cudaWaitExternalSemaphoresAsync(testing::NotNull(), testing::NotNull(), 1, stream))
        .WillOnce([&](const auto*, const auto* params, auto, auto) {
            EXPECT_EQ(params->params.fence.value, 1U);
            callOrder.push_back(1);
            return cudaErrorNotSupported;
        })
        .WillOnce([&](const auto*, const auto* params, auto, auto) {
            EXPECT_EQ(params->params.fence.value, 1U);
            callOrder.push_back(2);
            return cudaSuccess;
        })
        .WillOnce([&](const auto*, const auto* params, auto, auto) {
            EXPECT_EQ(params->params.fence.value, 3U);
            callOrder.push_back(5);
            return cudaSuccess;
        })
        .WillOnce([&](const auto*, const auto* params, auto, auto) {
            EXPECT_EQ(params->params.fence.value, 5U);
            callOrder.push_back(8);
            return cudaSuccess;
        })
        .WillOnce([&](const auto*, const auto* params, auto, auto) {
            EXPECT_EQ(params->params.fence.value, 7U);
            callOrder.push_back(11);
            return cudaSuccess;
        });
    EXPECT_CALL(cudaRuntime, cudaSignalExternalSemaphoresAsync(testing::NotNull(), testing::NotNull(), 1, stream))
        .WillOnce([&](const auto*, const auto* params, auto, auto) {
            EXPECT_EQ(params->params.fence.value, 2U);
            callOrder.push_back(4);
            return cudaSuccess;
        })
        .WillOnce([&](const auto*, const auto* params, auto, auto) {
            EXPECT_EQ(params->params.fence.value, 4U);
            callOrder.push_back(7);
            return cudaSuccess;
        })
        .WillOnce([&](const auto*, const auto* params, auto, auto) {
            EXPECT_EQ(params->params.fence.value, 6U);
            callOrder.push_back(10);
            return cudaSuccess;
        })
        .WillOnce([&](const auto*, const auto* params, auto, auto) {
            EXPECT_EQ(params->params.fence.value, 8U);
            callOrder.push_back(13);
            return cudaErrorNotSupported;
        });
    EXPECT_CALL(cudaRuntime, cudaStreamSynchronize(stream)).WillOnce(testing::Return(cudaSuccess));
    EXPECT_CALL(cudaRuntime, cudaDestroyExternalSemaphore(semaphore)).WillOnce(testing::Return(cudaSuccess));
    EXPECT_CALL(cudaRuntime, cudaFree(devicePointer)).WillOnce(testing::Return(cudaSuccess));
    EXPECT_CALL(cudaRuntime, cudaDestroyExternalMemory(memory)).WillOnce(testing::Return(cudaSuccess));
    EXPECT_CALL(cudaRuntime, cudaStreamDestroy(stream)).WillOnce(testing::Return(cudaSuccess));

    auto session = panda::cuda::CudaSession::create(*context);
    ASSERT_TRUE(session) << session.error().message;
    auto imported = session->importBuffer(std::move(*buffer));
    ASSERT_TRUE(imported) << imported.error().message;
    auto invalid = imported->submitWork(panda::ExternalTimelineValue {0}, {});
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().code, panda::ErrorCode::InvalidArgument);

    auto callbackCalled = false;
    const auto overflowValue =
        imported->submitWork(panda::ExternalTimelineValue {std::numeric_limits<std::uint64_t>::max()},
                             [&](auto, auto, auto) -> panda::Result<void> {
                                 callbackCalled = true;
                                 return {};
                             });
    ASSERT_FALSE(overflowValue);
    EXPECT_EQ(overflowValue.error().code, panda::ErrorCode::InvalidArgument);
    EXPECT_FALSE(callbackCalled);

    const auto wrongValue =
        imported->submitWork(panda::ExternalTimelineValue {2}, [&](auto, auto, auto) -> panda::Result<void> {
            callbackCalled = true;
            return {};
        });
    ASSERT_FALSE(wrongValue);
    EXPECT_EQ(wrongValue.error().code, panda::ErrorCode::InvalidArgument);
    EXPECT_FALSE(callbackCalled);

    const auto failedWait =
        imported->submitWork(panda::ExternalTimelineValue {1}, [&](auto, auto, auto) -> panda::Result<void> {
            callbackCalled = true;
            return {};
        });
    ASSERT_FALSE(failedWait);
    EXPECT_EQ(failedWait.error().code, panda::ErrorCode::Unsupported);
    EXPECT_FALSE(callbackCalled);

    const auto submitted = imported->submitWork(panda::ExternalTimelineValue {1},
                                                [&](auto callbackStream, auto* callbackBuffer, auto size) {
                                                    EXPECT_EQ(callbackStream, stream);
                                                    EXPECT_EQ(callbackBuffer, devicePointer);
                                                    EXPECT_EQ(size, 1024U);
                                                    callOrder.push_back(3);
                                                    return panda::Result<void> {};
                                                });
    ASSERT_TRUE(submitted) << submitted.error().message;
    EXPECT_EQ(submitted->signalValue.getValue(), 2U);
    EXPECT_TRUE(submitted->workResult);

    const auto failedWork =
        imported->submitWork(panda::ExternalTimelineValue {3}, [&](auto, auto*, auto) -> panda::Result<void> {
            callOrder.push_back(6);
            return std::unexpected {panda::makeError(panda::ErrorCode::BackendFailure, "injected callback failure")};
        });
    ASSERT_TRUE(failedWork) << failedWork.error().message;
    EXPECT_EQ(failedWork->signalValue.getValue(), 4U);
    ASSERT_FALSE(failedWork->workResult);
    EXPECT_EQ(failedWork->workResult.error().code, panda::ErrorCode::BackendFailure);

    const auto thrownWork =
        imported->submitWork(panda::ExternalTimelineValue {5}, [](auto, auto*, auto) -> panda::Result<void> {
            throw std::runtime_error {"injected callback exception"};
        });
    ASSERT_TRUE(thrownWork) << thrownWork.error().message;
    EXPECT_EQ(thrownWork->signalValue.getValue(), 6U);
    ASSERT_FALSE(thrownWork->workResult);
    EXPECT_NE(thrownWork->workResult.error().message.find("injected callback exception"), std::string::npos);

    auto callbackRanBeforeSignalFailure = false;
    const auto failedSignal = imported->submitWork(panda::ExternalTimelineValue {7}, [&](auto, auto*, auto) {
        callbackRanBeforeSignalFailure = true;
        callOrder.push_back(12);
        return panda::Result<void> {};
    });
    ASSERT_FALSE(failedSignal);
    EXPECT_EQ(failedSignal.error().code, panda::ErrorCode::Unsupported);
    EXPECT_TRUE(callbackRanBeforeSignalFailure);
    auto callbackRanAfterSignalFailure = false;
    const auto faultedHandoff = imported->submitWork(panda::ExternalTimelineValue {7}, [&](auto, auto*, auto) {
        callbackRanAfterSignalFailure = true;
        return panda::Result<void> {};
    });
    ASSERT_FALSE(faultedHandoff);
    EXPECT_EQ(faultedHandoff.error().code, panda::ErrorCode::BackendFailure);
    EXPECT_FALSE(callbackRanAfterSignalFailure);
    EXPECT_EQ(callOrder, (std::vector<int> {1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13}));

    *imported = panda::cuda::SharedCudaBuffer {};
    EXPECT_TRUE(session->close());
}
