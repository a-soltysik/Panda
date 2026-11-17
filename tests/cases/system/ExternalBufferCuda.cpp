#include <driver_types.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <limits>
#include <panda/Buffer.hpp>
#include <panda/Engine.hpp>
#include <panda/EngineOptions.hpp>
#include <panda/Error.hpp>
#include <panda/cuda/CudaSession.hpp>
#include <panda/cuda/SharedCudaBuffer.hpp>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "FakeHandle.hpp"
#include "ScopedMock.hpp"
#include "external/cuda/CudaRuntimeMock.hpp"

namespace
{
using panda::test::fakeHandle;
}

TEST(ExternalBufferCudaSystem, ReleasesImportedMemoryWhenSemaphoreImportFails)
{
    auto context =
        panda::Engine::create({.externalBufferSupport = panda::EngineOptions::ExternalBufferSupport::Required});
    if (!context && context.error().code == panda::ErrorCode::Unsupported)
    {
        GTEST_SKIP() << context.error().message;
    }
    ASSERT_TRUE(context) << context.error().message;

    auto buffer =
        context->getExternalBufferManager().createBuffer({.size = 1024, .usage = ::panda::BufferUsage::Storage});
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
    std::memcpy(std::span {properties.uuid.bytes}.data(), deviceUuid.data(), deviceUuid.size());
    auto* const stream = fakeHandle<cudaStream_t>(1);
    auto* const memory = fakeHandle<cudaExternalMemory_t>(2);
    auto* const devicePointer = fakeHandle<void*>(3);
    const auto semaphoreHandleValue = buffer->getSemaphoreHandle().get();
#ifdef _WIN32
    constexpr auto memoryHandleType = cudaExternalMemoryHandleTypeOpaqueWin32;
    constexpr auto semaphoreHandleType = cudaExternalSemaphoreHandleTypeTimelineSemaphoreWin32;
#else
    constexpr auto memoryHandleType = cudaExternalMemoryHandleTypeOpaqueFd;
    constexpr auto semaphoreHandleType = cudaExternalSemaphoreHandleTypeTimelineSemaphoreFd;
#endif

    ON_CALL(cudaRuntime, cudaGetDeviceCount(testing::NotNull())).WillByDefault([](auto* count) {
        *count = 1;
        return cudaSuccess;
    });
    EXPECT_CALL(cudaRuntime, cudaGetDeviceCount(testing::NotNull())).Times(1);
    ON_CALL(cudaRuntime, cudaGetDeviceProperties(testing::NotNull(), 0)).WillByDefault([&](auto* output, auto) {
        *output = properties;
        return cudaSuccess;
    });
    EXPECT_CALL(cudaRuntime, cudaGetDeviceProperties(testing::NotNull(), 0)).Times(1);
    EXPECT_CALL(cudaRuntime, cudaSetDevice(0)).Times(6).WillRepeatedly(testing::Return(cudaSuccess));
    ON_CALL(cudaRuntime, cudaStreamCreateWithFlags(testing::NotNull(), cudaStreamNonBlocking))
        .WillByDefault([&](auto* output, auto) {
            *output = stream;
            return cudaSuccess;
        });
    EXPECT_CALL(cudaRuntime, cudaStreamCreateWithFlags(testing::NotNull(), cudaStreamNonBlocking)).Times(1);
    ON_CALL(cudaRuntime, cudaImportExternalMemory(testing::NotNull(), testing::NotNull()))
        .WillByDefault([&](auto* output, const auto* descriptor) {
            EXPECT_EQ(descriptor->type, memoryHandleType);
            EXPECT_EQ(descriptor->flags, cudaExternalMemoryDedicated);
            *output = memory;
            return cudaSuccess;
        });
    ON_CALL(cudaRuntime, cudaExternalMemoryGetMappedBuffer(testing::NotNull(), memory, testing::NotNull()))
        .WillByDefault([&](auto* output, auto, const auto* descriptor) {
            EXPECT_EQ(descriptor->size, bufferSize);
            *output = devicePointer;
            return cudaSuccess;
        });
    ON_CALL(cudaRuntime, cudaImportExternalSemaphore(testing::NotNull(), testing::NotNull()))
        .WillByDefault([&](auto*, const auto* descriptor) {
            EXPECT_EQ(descriptor->type, semaphoreHandleType);
#ifdef _WIN32
            using NativeWin32Handle = decltype(std::declval<cudaExternalSemaphoreHandleDesc>().handle.win32.handle);
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast, performance-no-int-to-ptr): CUDA expects a Win32 HANDLE.
            EXPECT_EQ(descriptor->handle.win32.handle, reinterpret_cast<NativeWin32Handle>(semaphoreHandleValue));
#else
            EXPECT_EQ(descriptor->handle.fd, static_cast<int>(semaphoreHandleValue));
#endif
            return cudaErrorNotSupported;
        });
    ON_CALL(cudaRuntime, cudaStreamSynchronize(stream)).WillByDefault(testing::Return(cudaSuccess));
    ON_CALL(cudaRuntime, cudaFree(devicePointer)).WillByDefault(testing::Return(cudaSuccess));
    ON_CALL(cudaRuntime, cudaDestroyExternalMemory(memory)).WillByDefault(testing::Return(cudaSuccess));
    ON_CALL(cudaRuntime, cudaStreamDestroy(stream)).WillByDefault(testing::Return(cudaSuccess));
    {
        const auto orderedCalls = testing::InSequence {};
        EXPECT_CALL(cudaRuntime, cudaImportExternalMemory(testing::NotNull(), testing::NotNull())).Times(1);
        EXPECT_CALL(cudaRuntime, cudaExternalMemoryGetMappedBuffer(testing::NotNull(), memory, testing::NotNull()))
            .Times(1);
        EXPECT_CALL(cudaRuntime, cudaImportExternalSemaphore(testing::NotNull(), testing::NotNull())).Times(1);
        EXPECT_CALL(cudaRuntime, cudaStreamSynchronize(stream)).Times(1);
        EXPECT_CALL(cudaRuntime, cudaFree(devicePointer)).Times(1);
        EXPECT_CALL(cudaRuntime, cudaDestroyExternalMemory(memory)).Times(1);
    }
    EXPECT_CALL(cudaRuntime, cudaStreamDestroy(stream)).Times(1);
    auto session = panda::cuda::CudaSession::create(context->getDeviceInfo().deviceUuid);
    ASSERT_TRUE(session) << session.error().message;
    const auto imported = session->importBuffer(std::move(*buffer));
    ASSERT_FALSE(imported);
    EXPECT_EQ(imported.error().code, panda::ErrorCode::Unsupported);
    EXPECT_TRUE(session->close());
}

TEST(ExternalBufferCudaSystem, RejectsDeviceUuidMismatchBeforeNativeImport)
{
    auto context =
        panda::Engine::create({.externalBufferSupport = panda::EngineOptions::ExternalBufferSupport::Required});
    if (!context && context.error().code == panda::ErrorCode::Unsupported)
    {
        GTEST_SKIP() << context.error().message;
    }
    ASSERT_TRUE(context) << context.error().message;

    auto buffer =
        context->getExternalBufferManager().createBuffer({.size = 1024, .usage = ::panda::BufferUsage::Storage});
    if (!buffer && buffer.error().code == panda::ErrorCode::Unsupported)
    {
        GTEST_SKIP() << buffer.error().message;
    }
    ASSERT_TRUE(buffer) << buffer.error().message;

    auto cudaRuntime = panda::test::ScopedMock<panda::test::CudaRuntimeMock> {};
    auto mismatchedUuid = context->getDeviceInfo().deviceUuid;
    mismatchedUuid.front() ^= std::uint8_t {0xff};
    auto properties = cudaDeviceProp {};
    std::memcpy(std::span {properties.uuid.bytes}.data(), mismatchedUuid.data(), mismatchedUuid.size());
    auto* const stream = fakeHandle<cudaStream_t>(4);

    ON_CALL(cudaRuntime, cudaGetDeviceCount(testing::NotNull())).WillByDefault([](auto* count) {
        *count = 1;
        return cudaSuccess;
    });
    EXPECT_CALL(cudaRuntime, cudaGetDeviceCount(testing::NotNull())).Times(1);
    ON_CALL(cudaRuntime, cudaGetDeviceProperties(testing::NotNull(), 0)).WillByDefault([&](auto* output, auto) {
        *output = properties;
        return cudaSuccess;
    });
    EXPECT_CALL(cudaRuntime, cudaGetDeviceProperties(testing::NotNull(), 0)).Times(1);
    ON_CALL(cudaRuntime, cudaSetDevice(0)).WillByDefault(testing::Return(cudaSuccess));
    EXPECT_CALL(cudaRuntime, cudaSetDevice(0)).Times(2);
    ON_CALL(cudaRuntime, cudaStreamCreateWithFlags(testing::NotNull(), cudaStreamNonBlocking))
        .WillByDefault([&](auto* output, auto) {
            *output = stream;
            return cudaSuccess;
        });
    EXPECT_CALL(cudaRuntime, cudaStreamCreateWithFlags(testing::NotNull(), cudaStreamNonBlocking)).Times(1);
    EXPECT_CALL(cudaRuntime, cudaImportExternalMemory(testing::_, testing::_)).Times(0);
    EXPECT_CALL(cudaRuntime, cudaImportExternalSemaphore(testing::_, testing::_)).Times(0);
    ON_CALL(cudaRuntime, cudaStreamDestroy(stream)).WillByDefault(testing::Return(cudaSuccess));
    EXPECT_CALL(cudaRuntime, cudaStreamDestroy(stream)).Times(1);

    auto session = panda::cuda::CudaSession::create(mismatchedUuid);
    ASSERT_TRUE(session) << session.error().message;
    const auto imported = session->importBuffer(std::move(*buffer));
    ASSERT_FALSE(imported);
    EXPECT_EQ(imported.error().code, panda::ErrorCode::InvalidArgument);
    EXPECT_TRUE(session->close());
}

TEST(ExternalBufferCudaSystem, QueuesWorkBetweenTimelineWaitAndSignal)
{
    auto context =
        panda::Engine::create({.externalBufferSupport = panda::EngineOptions::ExternalBufferSupport::Required});
    if (!context && context.error().code == panda::ErrorCode::Unsupported)
    {
        GTEST_SKIP() << context.error().message;
    }
    ASSERT_TRUE(context) << context.error().message;

    auto buffer =
        context->getExternalBufferManager().createBuffer({.size = 1024, .usage = ::panda::BufferUsage::Storage});
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
    std::memcpy(std::span {properties.uuid.bytes}.data(), deviceUuid.data(), deviceUuid.size());
    auto* const stream = fakeHandle<cudaStream_t>(5);
    auto* const memory = fakeHandle<cudaExternalMemory_t>(6);
    auto* const semaphore = fakeHandle<cudaExternalSemaphore_t>(7);
    auto* const devicePointer = fakeHandle<void*>(8);
    auto callOrder = std::vector<int> {};

    constexpr auto waitValues = std::array<std::uint64_t, 5> {1, 1, 3, 5, 7};
    constexpr auto waitOrder = std::array {1, 2, 5, 8, 11};
    constexpr auto waitResults = std::array {cudaErrorNotSupported, cudaSuccess, cudaSuccess, cudaSuccess, cudaSuccess};
    auto waitIndex = std::size_t {};
    constexpr auto signalValues = std::array<std::uint64_t, 4> {2, 4, 6, 8};
    constexpr auto signalOrder = std::array {4, 7, 10, 13};
    constexpr auto signalResults = std::array {cudaSuccess, cudaSuccess, cudaSuccess, cudaErrorNotSupported};
    auto signalIndex = std::size_t {};
    ON_CALL(cudaRuntime, cudaGetDeviceCount(testing::NotNull())).WillByDefault([](auto* count) {
        *count = 1;
        return cudaSuccess;
    });
    EXPECT_CALL(cudaRuntime, cudaGetDeviceCount(testing::NotNull())).Times(1);
    ON_CALL(cudaRuntime, cudaGetDeviceProperties(testing::NotNull(), 0)).WillByDefault([&](auto* output, auto) {
        *output = properties;
        return cudaSuccess;
    });
    EXPECT_CALL(cudaRuntime, cudaGetDeviceProperties(testing::NotNull(), 0)).Times(1);
    EXPECT_CALL(cudaRuntime, cudaSetDevice(0)).Times(16).WillRepeatedly(testing::Return(cudaSuccess));
    ON_CALL(cudaRuntime, cudaStreamCreateWithFlags(testing::NotNull(), cudaStreamNonBlocking))
        .WillByDefault([&](auto* output, auto) {
            *output = stream;
            return cudaSuccess;
        });
    EXPECT_CALL(cudaRuntime, cudaStreamCreateWithFlags(testing::NotNull(), cudaStreamNonBlocking)).Times(1);
    ON_CALL(cudaRuntime, cudaImportExternalMemory(testing::NotNull(), testing::NotNull()))
        .WillByDefault([&](auto* output, const auto*) {
            *output = memory;
            return cudaSuccess;
        });
    EXPECT_CALL(cudaRuntime, cudaImportExternalMemory(testing::NotNull(), testing::NotNull())).Times(1);
    ON_CALL(cudaRuntime, cudaExternalMemoryGetMappedBuffer(testing::NotNull(), memory, testing::NotNull()))
        .WillByDefault([&](auto* output, auto, const auto*) {
            *output = devicePointer;
            return cudaSuccess;
        });
    EXPECT_CALL(cudaRuntime, cudaExternalMemoryGetMappedBuffer(testing::NotNull(), memory, testing::NotNull()))
        .Times(1);
    ON_CALL(cudaRuntime, cudaImportExternalSemaphore(testing::NotNull(), testing::NotNull()))
        .WillByDefault([&](auto* output, const auto*) {
            *output = semaphore;
            return cudaSuccess;
        });
    EXPECT_CALL(cudaRuntime, cudaImportExternalSemaphore(testing::NotNull(), testing::NotNull())).Times(1);
    ON_CALL(cudaRuntime, cudaWaitExternalSemaphoresAsync(testing::NotNull(), testing::NotNull(), 1, stream))
        .WillByDefault([&](const auto*, const auto* params, auto, auto) {
            EXPECT_EQ(params->params.fence.value, waitValues.at(waitIndex));
            callOrder.push_back(waitOrder.at(waitIndex));
            return waitResults.at(waitIndex++);
        });
    EXPECT_CALL(cudaRuntime, cudaWaitExternalSemaphoresAsync(testing::NotNull(), testing::NotNull(), 1, stream))
        .Times(5);
    ON_CALL(cudaRuntime, cudaSignalExternalSemaphoresAsync(testing::NotNull(), testing::NotNull(), 1, stream))
        .WillByDefault([&](const auto*, const auto* params, auto, auto) {
            EXPECT_EQ(params->params.fence.value, signalValues.at(signalIndex));
            callOrder.push_back(signalOrder.at(signalIndex));
            return signalResults.at(signalIndex++);
        });
    EXPECT_CALL(cudaRuntime, cudaSignalExternalSemaphoresAsync(testing::NotNull(), testing::NotNull(), 1, stream))
        .Times(4);
    ON_CALL(cudaRuntime, cudaStreamSynchronize(stream)).WillByDefault(testing::Return(cudaSuccess));
    EXPECT_CALL(cudaRuntime, cudaStreamSynchronize(stream)).Times(1);
    ON_CALL(cudaRuntime, cudaDestroyExternalSemaphore(semaphore)).WillByDefault(testing::Return(cudaSuccess));
    EXPECT_CALL(cudaRuntime, cudaDestroyExternalSemaphore(semaphore)).Times(1);
    ON_CALL(cudaRuntime, cudaFree(devicePointer)).WillByDefault(testing::Return(cudaSuccess));
    EXPECT_CALL(cudaRuntime, cudaFree(devicePointer)).Times(1);
    ON_CALL(cudaRuntime, cudaDestroyExternalMemory(memory)).WillByDefault(testing::Return(cudaSuccess));
    EXPECT_CALL(cudaRuntime, cudaDestroyExternalMemory(memory)).Times(1);
    ON_CALL(cudaRuntime, cudaStreamDestroy(stream)).WillByDefault(testing::Return(cudaSuccess));
    EXPECT_CALL(cudaRuntime, cudaStreamDestroy(stream)).Times(1);

    auto session = panda::cuda::CudaSession::create(context->getDeviceInfo().deviceUuid);
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
