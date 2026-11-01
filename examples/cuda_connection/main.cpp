#include <cuda_runtime_api.h>
#include <driver_types.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
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
#include <tuple>
#include <utility>
#include <vulkan/vulkan.hpp>

#include "ProducerBoundary.hpp"

namespace
{
constexpr auto handoffPassCount = std::uint32_t {3};

void runHandoffPass(panda::Context& context, panda::cuda::SharedCudaBuffer& sharedBuffer, std::uint32_t pass)
{
    const auto values = std::array {pass};
    auto uploadBatch = panda::expect(context.createUploadBatch(), "Create the upload batch");
    panda::expect(uploadBatch.write(sharedBuffer.getBuffer(), 0, std::as_bytes(std::span {values})),
                  "Stage the pass value");
    std::ignore = panda::expect(uploadBatch.submit(), "Submit the upload");

    auto& externalBuffers = context.getExternalBufferManager();
    const auto released =
        panda::expect(externalBuffers.releaseToExternal(sharedBuffer.getBuffer()), "Release the buffer to CUDA");
    const auto submitted =
        panda::expect(sharedBuffer.submitWork(
                          released,
                          [](cudaStream_t stream, std::byte* deviceBuffer, std::uint64_t size) -> panda::Result<void> {
                              panda::expect(cudaMemsetAsync(deviceBuffer, 0, size, stream) == cudaSuccess,
                                            "Clear the imported CUDA buffer");
                              return {};
                          }),
                      "Submit the CUDA clear");
    panda::expect(externalBuffers.acquireFromExternal(sharedBuffer.getBuffer(), submitted.signalValue),
                  "Reacquire the buffer for Vulkan");
    panda::expect(submitted.workResult, "Clear the imported CUDA buffer");

    std::println("Pass {}: Vulkan uploaded {}; CUDA queued a clear; Vulkan reacquired timeline {}",
                 pass,
                 values.front(),
                 submitted.signalValue.getValue());
}

void runSharedBuffer()
{
    auto context = panda::expect(
        panda::Context::create({.applicationName = "Panda CUDA connection",
                                .externalBufferSupport = panda::ContextOptions::ExternalBufferSupport::Required}),
        "Create a context with external-buffer support");
    auto session = panda::expect(panda::cuda::CudaSession::create(context), "Create a matching CUDA session");
    auto buffer =
        panda::expect(context.getExternalBufferManager().createBuffer(
                          {.size = sizeof(std::uint32_t),
                           .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst}),
                      "Create the exportable buffer");
    auto sharedBuffer = panda::expect(session.importBuffer(std::move(buffer)), "Import the buffer into CUDA");
    for (auto pass = std::uint32_t {1}; pass <= handoffPassCount; ++pass)
    {
        runHandoffPass(context, sharedBuffer, pass);
    }
    std::println("Completed {} Vulkan/CUDA passes on the {}-byte buffer for '{}'",
                 handoffPassCount,
                 sharedBuffer.getSize(),
                 context.getDeviceInfo().name);
}

}

auto main() -> int
{
    try
    {
        std::ignore = panda::log::Logger::instance().addSink(std::make_unique<panda::log::StreamSink>(std::clog));
        const auto availability = panda::expect(panda::cuda::queryAvailability(), "Query CUDA availability");
        if (!panda::shouldBe(availability == panda::cuda::Availability::Available,
                             "CUDA driver or device unavailable; Panda core remains usable"))
        {
            return EXIT_SUCCESS;
        }
        runSharedBuffer();
        const auto result = runProducer();
        panda::expect(result.nativeCode == 0, "Run the standalone CUDA producer");
        panda::expect(result.value == producedValue, "CUDA producer returned an unexpected value");
        std::println("CUDA producer returned {}", result.value);
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        panda::panic(error.what());
    }
}
