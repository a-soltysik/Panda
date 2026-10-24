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
#include <panda/cuda/Cuda.hpp>
#include <print>
#include <string_view>
#include <tuple>
#include <utility>
#include <vulkan/vulkan.hpp>

#include "ProducerBoundary.hpp"

namespace
{
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

auto importBuffer(const panda::Context& context, panda::cuda::CudaSession& session, std::string_view deviceName)
    -> panda::Result<void>
{
    auto buffer =
        context.createExternalBuffer({.size = sizeof(std::uint32_t), .usage = vk::BufferUsageFlagBits::eStorageBuffer});
    if (!buffer)
    {
        return std::unexpected {buffer.error()};
    }
    auto sharedBuffer = session.importBuffer(std::move(*buffer));
    if (!sharedBuffer)
    {
        return std::unexpected {sharedBuffer.error()};
    }

    std::println("Imported a {}-byte Vulkan buffer into CUDA on '{}'; no GPU work uses this buffer yet",
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
    catch (const std::exception& error)
    {
        return fail(error.what());
    }
    catch (...)
    {
        return fail("Unexpected application failure");
    }
}
