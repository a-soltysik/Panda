#include <cstdlib>
#include <exception>
#include <expected>
#include <panda/Assert.hpp>
#include <panda/Buffer.hpp>
#include <panda/Context.hpp>
#include <panda/Error.hpp>
#include <panda/Logger.hpp>
#include <string_view>
#include <vulkan/vulkan.hpp>

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

auto allocateBuffer(const panda::Context& context) -> panda::Result<void>
{
    auto created = context.createBuffer(
        {.size = 4096, .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst});
    if (!created)
    {
        return std::unexpected {created.error()};
    }
    panda::log::info("Allocated a {}-byte storage buffer on '{}'", created->getSize(), context.getDeviceInfo().name);
    // This scope releases the buffer before main's Context is destroyed.
    return {};
}
}

auto main() -> int
{
    try
    {
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
