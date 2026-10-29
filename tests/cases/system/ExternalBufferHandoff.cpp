// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <cuda_runtime.h>
#include <cuda_runtime_api.h>
#include <driver_types.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <expected>
#include <memory>
#include <panda/Context.hpp>
#include <panda/Error.hpp>
#include <panda/Logger.hpp>
#include <panda/cuda/Cuda.hpp>
#include <panda/cuda/CudaSession.hpp>
#include <panda/cuda/SharedCudaBuffer.hpp>
#include <span>
#include <utility>

#include "LogCapture.hpp"
#include "VulkanBufferReadback.hpp"

namespace
{
using Tag = std::array<std::byte, 4>;
using Observations = std::array<std::byte, 8>;

auto reverseOnCuda(cudaStream_t stream, std::span<std::byte, 4> buffer, std::span<std::byte, 4> scratch)
    -> panda::Result<void>
{
    const auto copied =
        cudaMemcpyAsync(scratch.data(), buffer.data(), scratch.size(), cudaMemcpyDeviceToDevice, stream);
    if (copied != cudaSuccess)
    {
        return std::unexpected {panda::makeError(panda::ErrorCode::BackendFailure, cudaGetErrorString(copied))};
    }
    for (auto index = std::size_t {0}; index < buffer.size(); ++index)
    {
        const auto written = cudaMemcpyAsync(buffer.subspan(index, 1).data(),
                                             scratch.subspan(buffer.size() - index - 1, 1).data(),
                                             1,
                                             cudaMemcpyDeviceToDevice,
                                             stream);
        if (written != cudaSuccess)
        {
            return std::unexpected {panda::makeError(panda::ErrorCode::BackendFailure, cudaGetErrorString(written))};
        }
    }
    return {};
}

void uploadTag(const panda::Context& context, const panda::cuda::SharedCudaBuffer& buffer, const Tag& tag)
{
    auto upload = context.createUploadBatch();
    ASSERT_TRUE(upload);
    ASSERT_TRUE(upload->write(buffer.getBuffer(), 0, tag));
    ASSERT_TRUE(upload->submit());
}

void queueTaggedPass(panda::Context& context,
                     panda::cuda::SharedCudaBuffer& buffer,
                     panda::test::VulkanBufferReadback& readback,
                     std::span<std::byte, 4> scratch,
                     const Tag& tag)
{
    ASSERT_NO_FATAL_FAILURE(uploadTag(context, buffer, tag));
    auto& external = context.getExternalBufferManager();
    const auto released = external.releaseToExternal(buffer.getBuffer());
    ASSERT_TRUE(released) << released.error().message;
    const auto submitted = buffer.submitWork(*released, [&](auto stream, auto* deviceBuffer, auto) {
        return reverseOnCuda(stream, std::span<std::byte, 4> {deviceBuffer, 4}, scratch);
    });
    ASSERT_TRUE(submitted) << submitted.error().message;
    ASSERT_TRUE(external.acquireFromExternal(buffer.getBuffer(), submitted->signalValue));
    ASSERT_TRUE(submitted->workResult) << submitted->workResult.error().message;
    ASSERT_TRUE(readback.submitCopy());
}

void exerciseBuffer(panda::Context& context,
                    panda::cuda::CudaSession& session,
                    std::span<std::byte, 4> scratch,
                    Observations& observed)
{
    auto external = context.getExternalBufferManager().createBuffer(
        {.size = 4, .usage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc});
    ASSERT_TRUE(external) << external.error().message;
    auto buffer = session.importBuffer(std::move(*external));
    ASSERT_TRUE(buffer) << buffer.error().message;
    auto readback = panda::test::VulkanBufferReadback::create(context, buffer->getBuffer());
    ASSERT_TRUE(readback) << readback.error().message;
    const auto first = Tag {std::byte {0x12}, std::byte {0x34}, std::byte {0x56}, std::byte {0x78}};
    const auto second = Tag {std::byte {0x87}, std::byte {0x65}, std::byte {0x43}, std::byte {0x21}};
    for (const auto& tag : std::array {first, second})
    {
        queueTaggedPass(context, *buffer, **readback, scratch, tag);
        if (testing::Test::HasFatalFailure())
        {
            return;
        }
    }
    const auto result = (*readback)->read();
    ASSERT_TRUE(result) << result.error().message;
    observed = *result;
}

void exerciseWithScratch(panda::Context& context, panda::cuda::CudaSession& session, Observations& observed)
{
    auto releaseScratch = [](std::byte* memory) {
        EXPECT_EQ(cudaFree(memory), cudaSuccess);
    };
    std::byte* memory {};
    ASSERT_EQ(cudaMalloc(&memory, 4), cudaSuccess);
    auto owner = std::unique_ptr<std::byte, decltype(releaseScratch)> {memory, releaseScratch};
    ASSERT_NO_FATAL_FAILURE(exerciseBuffer(context, session, std::span<std::byte, 4> {owner.get(), 4}, observed));
}

void runTaggedHandoffs(Observations& observed)
{
    auto context =
        panda::Context::create({.applicationName = "Panda tagged CUDA handoff",
                                .enableValidation = true,
                                .externalBufferSupport = panda::ContextOptions::ExternalBufferSupport::Required});
    if (!context && context.error().code == panda::ErrorCode::Unsupported)
    {
        GTEST_SKIP() << context.error().message;
    }
    ASSERT_TRUE(context) << context.error().message;
    if (!context->getDeviceInfo().externalSemaphoreExportEnabled)
    {
        GTEST_SKIP() << "External timeline semaphore export is unavailable";
    }
    auto session = panda::cuda::CudaSession::create(*context);
    ASSERT_TRUE(session) << session.error().message;
    ASSERT_NO_FATAL_FAILURE(exerciseWithScratch(*context, *session, observed));
}
}

TEST(ExternalBufferHandoffSystem, VulkanReadsCudaResultsAfterTwoConsecutiveHandoffs)
{
    const auto availability = panda::cuda::queryAvailability();
    ASSERT_TRUE(availability);
    if (*availability != panda::cuda::Availability::Available)
    {
        GTEST_SKIP() << "CUDA runtime has no usable device";
    }
    auto observed = Observations {};
    auto records = panda::test::LogRecords {};
    auto capture = panda::test::ProcessSinkRegistration {records};
    ASSERT_NO_FATAL_FAILURE(runTaggedHandoffs(observed));
    if (testing::Test::IsSkipped())
    {
        return;
    }
    const auto expected = Observations {std::byte {0x78},
                                        std::byte {0x56},
                                        std::byte {0x34},
                                        std::byte {0x12},
                                        std::byte {0x21},
                                        std::byte {0x43},
                                        std::byte {0x65},
                                        std::byte {0x87}};
    EXPECT_EQ(observed, expected);
    EXPECT_FALSE(std::ranges::any_of(records.entries, [](const auto& entry) {
        return entry.level == panda::log::Level::Error;
    }));
}
