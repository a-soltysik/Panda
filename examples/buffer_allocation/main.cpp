#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <expected>
#include <iostream>
#include <memory>
#include <panda/Assert.hpp>
#include <panda/Buffer.hpp>
#include <panda/CompletionPoint.hpp>
#include <panda/Context.hpp>
#include <panda/Error.hpp>
#include <panda/Logger.hpp>
#include <panda/UploadBatch.hpp>
#include <span>
#include <string_view>
#include <tuple>
#include <vulkan/vulkan.hpp>

namespace
{
constexpr auto bufferSize = std::uint64_t {4096};
constexpr auto uploadTimeout = std::chrono::seconds {10};

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

auto uploadValues(const panda::Context& context, const panda::Buffer& buffer) -> panda::Result<void>
{
    const auto values = std::array<std::uint32_t, 4> {1, 2, 3, 4};
    auto batch = context.createUploadBatch();
    if (!batch)
    {
        return std::unexpected {batch.error()};
    }
    const auto written = batch->write(buffer, 0, std::as_bytes(std::span {values}));
    if (!written)
    {
        return written;
    }
    return batch->submit()
        .and_then([&](panda::CompletionPoint point) {
            return context.wait(point, uploadTimeout);
        })
        .and_then([](panda::WaitStatus status) -> panda::Result<void> {
            if (status == panda::WaitStatus::Timeout)
            {
                return std::unexpected {
                    panda::makeError(panda::ErrorCode::Timeout, "Buffer upload did not finish in time")};
            }
            panda::log::info("Uploaded four values to the storage buffer");
            return {};
        });
}

auto allocateBuffer(const panda::Context& context) -> panda::Result<void>
{
    auto created = context.createBuffer(
        {.size = bufferSize, .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst});
    if (!created)
    {
        return std::unexpected {created.error()};
    }
    panda::log::info("Allocated a {}-byte storage buffer on '{}'", created->getSize(), context.getDeviceInfo().name);
    return uploadValues(context, *created);
}
}

auto main() -> int
{
    try
    {
        std::ignore = panda::log::Logger::instance().addSink(std::make_unique<panda::log::StreamSink>(std::clog));
        const auto result =
            panda::Context::create({.applicationName = "Panda buffer allocation", .enableValidation = true})
                .and_then([](const panda::Context& context) {
                    return allocateBuffer(context);
                });
        return result ? EXIT_SUCCESS : fail(result.error().message);
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
