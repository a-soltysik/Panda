// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include "upload/UploadCommand.hpp"

#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <panda/Assert.hpp>
#include <panda/Error.hpp>
#include <span>
#include <variant>

#include "upload/UploadOperation.hpp"
// BufferUpload only forward declares this owner type; recording calls its accessors.
#include "resources/BufferResource.hpp"  // IWYU pragma: keep
#include "resources/TextureResource.hpp"

namespace panda::core::vulkan::upload
{
using api_detail::BufferResource;
using api_detail::TextureResource;

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

void transitionToTransfer(vk::CommandBuffer command, const TextureResource& texture)
{
    const auto barrier = vk::ImageMemoryBarrier2 {
        .srcStageMask = vk::PipelineStageFlagBits2::eNone,
        .srcAccessMask = vk::AccessFlagBits2::eNone,
        .dstStageMask = vk::PipelineStageFlagBits2::eCopy,
        .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .oldLayout = vk::ImageLayout::eUndefined,
        .newLayout = vk::ImageLayout::eTransferDstOptimal,
        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
        .image = texture.getNative(),
        .subresourceRange = vk::ImageSubresourceRange {.aspectMask = vk::ImageAspectFlagBits::eColor,
                                                       .baseMipLevel = 0,
                                                       .levelCount = 1,
                                                       .baseArrayLayer = 0,
                                                       .layerCount = 1}
    };
    command.pipelineBarrier2(vk::DependencyInfo {.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier});
}

void transitionToSampled(vk::CommandBuffer command, const TextureResource& texture)
{
    const auto barrier = vk::ImageMemoryBarrier2 {
        .srcStageMask = vk::PipelineStageFlagBits2::eCopy,
        .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
        .dstAccessMask = vk::AccessFlagBits2::eShaderSampledRead,
        .oldLayout = vk::ImageLayout::eTransferDstOptimal,
        .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
        .image = texture.getNative(),
        .subresourceRange = vk::ImageSubresourceRange {.aspectMask = vk::ImageAspectFlagBits::eColor,
                                                       .baseMipLevel = 0,
                                                       .levelCount = 1,
                                                       .baseArrayLayer = 0,
                                                       .layerCount = 1}
    };
    command.pipelineBarrier2(vk::DependencyInfo {.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier});
}

void recordCopy(vk::CommandBuffer command, const BufferUpload& buffer)
{
    orderBeforeCopy(command);
    const auto region =
        vk::BufferCopy {.srcOffset = buffer.stagingOffset, .dstOffset = buffer.offset, .size = buffer.size};
    command.copyBuffer(buffer.staging->getNative(), buffer.destination->getNative(), region);
}

void recordCopy(vk::CommandBuffer command, const TextureUpload& texture)
{
    orderBeforeCopy(command);
    if (texture.firstForTexture)
    {
        transitionToTransfer(command, *texture.destination);
    }
    const auto copy = vk::BufferImageCopy {
        .bufferOffset = texture.stagingOffset,
        .bufferRowLength = texture.bufferRowLength,
        .bufferImageHeight = 0,
        .imageSubresource = vk::ImageSubresourceLayers {.aspectMask = vk::ImageAspectFlagBits::eColor,
                                                        .mipLevel = 0,
                                                        .baseArrayLayer = 0,
                                                        .layerCount = 1},
        .imageOffset = vk::Offset3D {.x = texture.region.offset.x, .y = texture.region.offset.y, .z = 0},
        .imageExtent =
            vk::Extent3D {.width = texture.region.extent.width, .height = texture.region.extent.height, .depth = 1}
    };
    command.copyBufferToImage(texture.staging->getNative(),
                              texture.destination->getNative(),
                              vk::ImageLayout::eTransferDstOptimal,
                              copy);
}

void transitionToSampledAfterCopies([[maybe_unused]] vk::CommandBuffer command,
                                    [[maybe_unused]] const BufferUpload& upload)
{
}

void transitionToSampledAfterCopies(vk::CommandBuffer command, const TextureUpload& texture)
{
    if (texture.firstForTexture)
    {
        transitionToSampled(command, *texture.destination);
    }
}
}

UploadCommand::UploadCommand(std::span<const UploadOperation> writes,
                             std::span<const std::shared_ptr<BufferResource>> stagingPages)
    : _writes {writes.begin(), writes.end()},
      _stagingPages {stagingPages.begin(), stagingPages.end()}
{
}

auto UploadCommand::create(const vk::raii::Device& device,
                           std::uint32_t family,
                           std::span<const UploadOperation> writes,
                           std::span<const std::shared_ptr<BufferResource>> stagingPages)
    -> Result<std::unique_ptr<UploadCommand>>
{
    auto packet = std::unique_ptr<UploadCommand> {
        new UploadCommand {writes, stagingPages}
    };
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
        std::visit(
            [&](const auto& upload) {
                recordCopy(_command, upload);
            },
            write);
    }
    makeCopiesVisible(_command);
    for (const auto& write : _writes)
    {
        std::visit(
            [&](const auto& upload) {
                transitionToSampledAfterCopies(_command, upload);
            },
            write);
    }
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
    for (const auto& page : _stagingPages)
    {
        const auto reserved = page->getReservedSize();
        expect(reserved <= std::numeric_limits<std::uint64_t>::max() - bytes, "Upload staging size overflow");
        bytes += reserved;
    }
    return bytes;
}
}
