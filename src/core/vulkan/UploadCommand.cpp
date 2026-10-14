// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include "UploadCommand.hpp"

#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <panda/Assert.hpp>
#include <panda/Error.hpp>
#include <span>

#include "UploadBatchImpl.hpp"
#include "VulkanError.hpp"
// BufferUpload only forward declares this owner type; recording calls its accessors.
#include "resources/BufferResource.hpp"  // IWYU pragma: keep

namespace panda::detail
{
namespace
{
void orderBeforeCopy(vk::CommandBuffer command)
{
    const auto barrier =
        vk::MemoryBarrier2 {.srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
                            .srcAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
                            .dstStageMask = vk::PipelineStageFlagBits2::eCopy,
                            .dstAccessMask = vk::AccessFlagBits2::eTransferWrite};
    command.pipelineBarrier2(vk::DependencyInfo {.memoryBarrierCount = 1, .pMemoryBarriers = &barrier});
}

void makeCopiesVisible(vk::CommandBuffer command)
{
    const auto barrier = vk::MemoryBarrier2 {
        .srcStageMask = vk::PipelineStageFlagBits2::eCopy,
        .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands | vk::PipelineStageFlagBits2::eHost,
        .dstAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite};
    command.pipelineBarrier2(vk::DependencyInfo {.memoryBarrierCount = 1, .pMemoryBarriers = &barrier});
}
}

UploadCommand::UploadCommand(std::span<const BufferUpload> writes)
    : _writes {writes.begin(), writes.end()}
{
}

auto UploadCommand::create(const vk::raii::Device& device, std::uint32_t family, std::span<const BufferUpload> writes)
    -> Result<std::unique_ptr<UploadCommand>>
{
    auto packet = std::unique_ptr<UploadCommand> {new UploadCommand {writes}};
    const auto initialized = packet->initialize(device, family);
    if (!initialized)
    {
        return std::unexpected {initialized.error()};
    }
    return packet;
}

auto UploadCommand::initialize(const vk::raii::Device& device, std::uint32_t family) -> Result<void>
{
    const auto poolInfo =
        vk::CommandPoolCreateInfo {.flags = vk::CommandPoolCreateFlagBits::eTransient, .queueFamilyIndex = family};
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

auto UploadCommand::record() const -> Result<void>
{
    const auto begun =
        _command.begin(vk::CommandBufferBeginInfo {.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
    if (begun != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(begun)};
    }
    for (const auto& write : _writes)
    {
        orderBeforeCopy(_command);
        const auto region = vk::BufferCopy {.srcOffset = 0, .dstOffset = write.offset, .size = write.size};
        _command.copyBuffer(write.staging->getNative(), write.destination->getNative(), region);
    }
    makeCopiesVisible(_command);
    const auto ended = _command.end();
    if (ended != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(ended)};
    }
    return {};
}

auto UploadCommand::getCommand() const -> vk::CommandBuffer
{
    return _command;
}

auto UploadCommand::getStagingSize() const -> std::uint64_t
{
    auto bytes = std::uint64_t {0};
    for (const auto& write : _writes)
    {
        const auto reserved = write.staging->getReservedSize();
        expect(reserved <= std::numeric_limits<std::uint64_t>::max() - bytes, "Upload staging size overflow");
        bytes += reserved;
    }
    return bytes;
}
}
