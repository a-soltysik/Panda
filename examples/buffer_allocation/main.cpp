#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <panda/Engine.hpp>
#include <panda/diagnostics/Assert.hpp>
#include <panda/diagnostics/Logger.hpp>
#include <panda/resources/Buffer.hpp>
#include <panda/submission/CompletionPoint.hpp>
#include <panda/submission/UploadBatch.hpp>
#include <span>
#include <tuple>

auto main() -> int
{
    try
    {
        std::ignore = panda::log::Logger::instance().addSink(std::make_unique<panda::log::StreamSink>(std::clog));
        const auto context = panda::expect(
            panda::Engine::create({.applicationName = "Panda buffer allocation", .enableValidation = true}),
            "Create the Vulkan context");
        const auto buffer = panda::expect(
            context.getResourceManager().createBuffer(
                {.size = 4096, .usage = ::panda::BufferUsage::Storage | ::panda::BufferUsage::TransferDestination}),
            "Allocate the storage buffer");
        static constexpr auto values = std::array<std::uint32_t, 4> {1, 2, 3, 4};
        auto batch = panda::expect(context.getSubmissionManager().createUploadBatch(), "Create the upload batch");
        panda::expect(batch.write(buffer, 0, std::as_bytes(std::span {values})), "Stage four values");
        const auto completion = panda::expect(batch.submit(), "Submit the upload");
        const auto status = panda::expect(context.getSubmissionManager().wait(completion, std::chrono::seconds {10}),
                                          "Wait for the upload");
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
