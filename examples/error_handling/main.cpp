#include <cstdlib>
#include <exception>
#include <expected>
#include <iostream>
#include <memory>
#include <panda/Assert.hpp>
#include <panda/Buffer.hpp>
#include <panda/Context.hpp>
#include <panda/Error.hpp>
#include <panda/Logger.hpp>
#include <tuple>
#include <utility>
#include <vulkan/vulkan.hpp>

namespace
{
void reportError(const panda::Error& error)
{
    panda::log::error("Error {}: {} ({}:{}, {})",
                      std::to_underlying(error.code),
                      error.message,
                      error.source.file_name(),
                      error.source.line(),
                      error.source.function_name());
    if (error.native)
    {
        panda::log::error("Native {} status: {}", error.native->api, error.native->code);
    }
}

auto allocateWithRetry(const panda::Context& context) -> panda::Result<void>
{
    auto description = panda::BufferDesc {.size = 0, .usage = vk::BufferUsageFlagBits::eStorageBuffer};
    auto buffer = context.createBuffer(description);
    panda::expect(!buffer, "A zero-byte buffer must be rejected");
    reportError(buffer.error());
    if (buffer.error().code != panda::ErrorCode::InvalidArgument)
    {
        return std::unexpected {buffer.error()};
    }
    // Correct the request; backend or device failures need a different recovery policy.
    description.size = 4096;
    buffer = context.createBuffer(description);
    if (!buffer)
    {
        return std::unexpected {buffer.error()};
    }
    panda::log::info("Recovered by allocating a {}-byte buffer", buffer->getSize());
    return {};
}
}

auto main() -> int
{
    try
    {
        std::ignore = panda::log::Logger::instance().addSink(std::make_unique<panda::log::StreamSink>(std::clog));
        const auto result =
            panda::Context::create({.applicationName = "Panda error handling", .enableValidation = true})
                .and_then(allocateWithRetry);
        if (!result)
        {
            reportError(result.error());
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        panda::panic(error.what());
    }
}
