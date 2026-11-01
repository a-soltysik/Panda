#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <panda/Assert.hpp>
#include <panda/Buffer.hpp>
#include <panda/CompletionPoint.hpp>
#include <panda/Context.hpp>
#include <panda/Logger.hpp>
#include <panda/UploadBatch.hpp>
#include <span>
#include <tuple>
#include <vulkan/vulkan.hpp>

auto main() -> int
{
    try
    {
        std::ignore = panda::log::Logger::instance().addSink(std::make_unique<panda::log::StreamSink>(std::clog));
        const auto context = panda::expect(
            panda::Context::create({.applicationName = "Panda buffer allocation", .enableValidation = true}),
            "Create the Vulkan context");
        const auto buffer = panda::expect(context.createBuffer({.size = 4096,
                                                                .usage = vk::BufferUsageFlagBits::eStorageBuffer |
                                                                         vk::BufferUsageFlagBits::eTransferDst}),
                                          "Allocate the storage buffer");
        static constexpr auto values = std::array<std::uint32_t, 4> {1, 2, 3, 4};
        auto batch = panda::expect(context.createUploadBatch(), "Create the upload batch");
        panda::expect(batch.write(buffer, 0, std::as_bytes(std::span {values})), "Stage four values");
        const auto completion = panda::expect(batch.submit(), "Submit the upload");
        const auto status = panda::expect(context.wait(completion, std::chrono::seconds {10}), "Wait for the upload");
        panda::expect(status == panda::WaitStatus::Completed, "Buffer upload did not finish in time");
        panda::log::info("Uploaded four values to the {}-byte storage buffer on '{}'",
                         buffer.getSize(),
                         context.getDeviceInfo().name);
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        panda::panic(error.what());
    }
}
