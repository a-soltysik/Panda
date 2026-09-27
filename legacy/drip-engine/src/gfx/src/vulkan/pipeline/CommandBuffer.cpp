// clang-format off
#include <drip/common/utils/Assert.hpp>
// clang-format on

#include "CommandBuffer.hpp"

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_structs.hpp>

#include "drip/gfx/utils/format/ResultFormatter.hpp"  //NOLINT(misc-include-cleaner)
#include "vulkan/core/Device.hpp"

namespace drip::gfx
{

auto CommandBuffer::beginSingleTimeCommandBuffer(const Device& device) noexcept -> vk::CommandBuffer
{
    const auto allocationInfo = vk::CommandBufferAllocateInfo {.commandPool = device.commandPool,
                                                               .level = vk::CommandBufferLevel::ePrimary,
                                                               .commandBufferCount = 1};
    const auto commandBuffer = common::Expect(device.logicalDevice.allocateCommandBuffers(allocationInfo),
                                              vk::Result::eSuccess,
                                              "Can't allocate command buffer")
                                   .result();
    static constexpr auto beginInfo =
        vk::CommandBufferBeginInfo {.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
    common::Expect(commandBuffer.front().begin(beginInfo), vk::Result::eSuccess, "Couldn't begin command buffer");
    return commandBuffer.front();
}

void CommandBuffer::endSingleTimeCommandBuffer(const Device& device, vk::CommandBuffer buffer) noexcept
{
    common::Expect(buffer.end(), vk::Result::eSuccess, "Couldn't end command buffer");

    const auto submitInfo = vk::SubmitInfo {.commandBufferCount = 1, .pCommandBuffers = &buffer};
    common::Expect(device.graphicsQueue.submit(submitInfo), vk::Result::eSuccess, "Couldn't submit graphics queue");
    common::ShouldBe(device.graphicsQueue.waitIdle(), vk::Result::eSuccess, "Couldn't wait idle on graphics queue");
    device.logicalDevice.free(device.commandPool, buffer);
}
}
