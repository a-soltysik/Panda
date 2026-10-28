// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
// clang-format on

#include "external/ExternalBufferOwnershipTransferCommand.hpp"

#include <cstdint>
#include <expected>
#include <memory>
#include <panda/Buffer.hpp>
#include <panda/Error.hpp>
#include <utility>

#include "resources/BufferResource.hpp"

namespace panda::detail
{
ExternalBufferOwnershipTransferCommand::ExternalBufferOwnershipTransferCommand(Buffer bufferOwner,
                                                                               vk::BufferMemoryBarrier2 barrier)
    : _bufferOwner {std::move(bufferOwner)},
      _barrier {barrier}
{
}

auto ExternalBufferOwnershipTransferCommand::create(const vk::raii::Device& device,
                                                    Buffer bufferOwner,
                                                    std::uint32_t family,
                                                    ExternalBufferOwnershipTransferDirection direction)
    -> Result<std::unique_ptr<ExternalBufferOwnershipTransferCommand>>
{
    const auto resource = BufferResource::getResource(bufferOwner);
    const auto release = direction == ExternalBufferOwnershipTransferDirection::Release;
    constexpr auto memoryAccess = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite;
    const auto barrier = vk::BufferMemoryBarrier2 {
        .srcStageMask = release ? vk::PipelineStageFlagBits2::eAllCommands : vk::PipelineStageFlagBits2::eNone,
        .srcAccessMask = release ? memoryAccess : vk::AccessFlags2 {},
        .dstStageMask = release ? vk::PipelineStageFlagBits2::eNone : vk::PipelineStageFlagBits2::eAllCommands,
        .dstAccessMask = release ? vk::AccessFlags2 {} : memoryAccess,
        .srcQueueFamilyIndex = release ? family : vk::QueueFamilyExternal,
        .dstQueueFamilyIndex = release ? vk::QueueFamilyExternal : family,
        .buffer = resource->getNative(),
        .offset = 0,
        .size = resource->getSize()};
    auto command = std::unique_ptr<ExternalBufferOwnershipTransferCommand> {
        new ExternalBufferOwnershipTransferCommand {std::move(bufferOwner), barrier}
    };
    if (const auto initialized = command->initialize(device); !initialized)
    {
        return std::unexpected {initialized.error()};
    }
    return command;
}

auto ExternalBufferOwnershipTransferCommand::initialize(const vk::raii::Device& device) -> Result<void>
{
    const auto poolInfo = vk::CommandPoolCreateInfo {
        .flags = vk::CommandPoolCreateFlagBits::eTransient,
        .queueFamilyIndex = _barrier.srcQueueFamilyIndex == vk::QueueFamilyExternal ? _barrier.dstQueueFamilyIndex
                                                                                    : _barrier.srcQueueFamilyIndex};
    auto pool = vk::CommandPool {};
    const auto created = (*device).createCommandPool(&poolInfo, nullptr, &pool, *device.getDispatcher());
    if (created != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(created)};
    }
    _pool = vk::raii::CommandPool {device, pool};
    const auto info = vk::CommandBufferAllocateInfo {.commandPool = *_pool,
                                                     .level = vk::CommandBufferLevel::ePrimary,
                                                     .commandBufferCount = 1};
    const auto allocated = (*device).allocateCommandBuffers(&info, &_command, *device.getDispatcher());
    if (allocated != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(allocated)};
    }
    return record();
}

auto ExternalBufferOwnershipTransferCommand::record() const -> Result<void>
{
    const auto begun =
        _command.begin(vk::CommandBufferBeginInfo {.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
    if (begun != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(begun)};
    }
    _command.pipelineBarrier2(vk::DependencyInfo {.bufferMemoryBarrierCount = 1, .pBufferMemoryBarriers = &_barrier});
    const auto ended = _command.end();
    if (ended != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(ended)};
    }
    return {};
}

auto ExternalBufferOwnershipTransferCommand::getCommand() const -> vk::CommandBuffer
{
    return _command;
}
}
