#include <driver_types.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <panda/Context.hpp>
#include <panda/Error.hpp>
#include <panda/cuda/CudaSession.hpp>
#include <span>
#include <utility>
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
