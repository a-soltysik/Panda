#include <cuda_runtime_api.h>
#include <driver_types.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>  // IWYU pragma: keep (stderr)
#include <cstdlib>
#include <exception>
#include <expected>
#include <iostream>
#include <memory>
#include <panda/Assert.hpp>
#include <panda/Context.hpp>
#include <panda/Error.hpp>
#include <panda/Logger.hpp>
#include <panda/UploadBatch.hpp>
#include <panda/cuda/Cuda.hpp>
#include <panda/cuda/CudaSession.hpp>
#include <panda/cuda/SharedCudaBuffer.hpp>
#include <print>
#include <span>
#include <string_view>
#include <tuple>
#include <utility>
#include <vulkan/vulkan.hpp>

#include "ProducerBoundary.hpp"

namespace
{
constexpr auto handoffPassCount = std::uint32_t {3};

auto fail(std::string_view message) noexcept -> int
{
    try
    {
        panda::log::error("{}", message);
    }
    catch (...)
    {
        panda::panic(message);
    }
    return EXIT_FAILURE;
}

auto runKernel() -> int
{
    const auto result = runProducer();
    if (result.nativeCode != 0 || result.value != producedValue)
    {
        std::println(stderr, "CUDA producer failed (native status {}, value {})", result.nativeCode, result.value);
        return EXIT_FAILURE;
    }
    std::println("CUDA producer returned {}", result.value);
    return EXIT_SUCCESS;
}

auto clearBuffer(cudaStream_t stream, std::byte* deviceBuffer, std::uint64_t size) -> panda::Result<void>
{
    const auto status = cudaMemsetAsync(deviceBuffer, 0, size, stream);
    if (status != cudaSuccess)
    {
        return std::unexpected {panda::makeError(panda::ErrorCode::BackendFailure,
                                                 "CUDA could not clear the imported buffer",
                                                 panda::NativeError {.api = "CUDA", .code = status})};
    }
    return {};
}

auto runHandoffPass(panda::Context& context, panda::cuda::SharedCudaBuffer& sharedBuffer, std::uint32_t pass) -> void
{
    const auto values = std::array {pass};
    auto uploadBatch = context.createUploadBatch().value();
    uploadBatch.write(sharedBuffer.getBuffer(), 0, std::as_bytes(std::span {values})).value();
    std::ignore = uploadBatch.submit().value();

    auto& externalBuffers = context.getExternalBufferManager();
    const auto released = externalBuffers.releaseToExternal(sharedBuffer.getBuffer()).value();
    const auto submitted = sharedBuffer.submitWork(released, clearBuffer).value();
    externalBuffers.acquireFromExternal(sharedBuffer.getBuffer(), submitted.signalValue).value();
    submitted.workResult.value();

    std::println("Pass {}: Vulkan uploaded {}; CUDA queued a clear; Vulkan reacquired timeline {}",
                 pass,
                 values.front(),
                 submitted.signalValue.getValue());
}

auto importBuffer(panda::Context& context, panda::cuda::CudaSession& session, std::string_view deviceName)
    -> panda::Result<void>
{
    auto buffer = context.getExternalBufferManager().createBuffer(
        {.size = sizeof(std::uint32_t),
         .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst});
    if (!buffer)
    {
        return std::unexpected {buffer.error()};
    }
    auto sharedBuffer = session.importBuffer(std::move(*buffer));
    if (!sharedBuffer)
    {
        return std::unexpected {sharedBuffer.error()};
    }

    for (auto pass = std::uint32_t {1}; pass <= handoffPassCount; ++pass)
    {
        runHandoffPass(context, *sharedBuffer, pass);
    }
    std::println("Completed {} Vulkan/CUDA passes on the {}-byte buffer for '{}'",
                 handoffPassCount,
                 sharedBuffer->getSize(),
                 deviceName);
    return {};
}

auto importSharedBuffer() -> panda::Result<void>
{
    auto context =
        panda::Context::create({.applicationName = "Panda CUDA connection",
                                .externalBufferSupport = panda::ContextOptions::ExternalBufferSupport::Optional});
    if (!context)
    {
        return std::unexpected {context.error()};
    }
    const auto deviceInfo = context->getDeviceInfo();
    if (!deviceInfo.externalBufferExportEnabled)
    {
        return std::unexpected {
            panda::makeError(panda::ErrorCode::Unsupported, "Vulkan external buffer export is unavailable")};
    }
    if (!deviceInfo.externalSemaphoreExportEnabled)
    {
        return std::unexpected {panda::makeError(panda::ErrorCode::Unsupported,
                                                 "Vulkan external timeline semaphore export is unavailable")};
    }
    auto session = panda::cuda::CudaSession::create(*context);
    if (!session)
    {
        return std::unexpected {session.error()};
    }
    return importBuffer(*context, *session, deviceInfo.name);
}

auto runApplication() -> int
{
    std::ignore = panda::log::Logger::instance().addSink(std::make_unique<panda::log::StreamSink>(std::clog));
    const auto availability = panda::cuda::queryAvailability();
    if (!availability)
    {
        std::println(stderr, "CUDA initialization failed (native status {})", availability.error().nativeCode);
        return EXIT_FAILURE;
    }
    if (*availability != panda::cuda::Availability::Available)
    {
        std::println("CUDA {} unavailable; Panda core remains usable",
                     *availability == panda::cuda::Availability::NoDriver ? "driver" : "device");
        return EXIT_SUCCESS;
    }
    const auto sharedBuffer = importSharedBuffer();
    if (!sharedBuffer && sharedBuffer.error().code != panda::ErrorCode::Unsupported)
    {
        return fail(sharedBuffer.error().message);
    }
    if (!sharedBuffer)
    {
        std::println("Shared Vulkan/CUDA buffer unavailable: {}", sharedBuffer.error().message);
    }
    return runKernel();
}
}

auto main() -> int
{
    try
    {
        return runApplication();
    }
    catch (const std::bad_expected_access<panda::Error>& error)
    {
        return fail(error.error().message);
    }
    catch (const std::exception& error)
    {
        return fail(error.what());
    }
    catch (...)
    {
        return fail("Unexpected application failure");
    }
}
