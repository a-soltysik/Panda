// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include "VulkanBufferReadback.hpp"

#include <gtest/gtest.h>
#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <limits>
#include <memory>
#include <panda/Buffer.hpp>
#include <panda/Engine.hpp>
#include <panda/Error.hpp>
#include <span>
#include <utility>

#include "VulkanError.hpp"
#include "resources/BufferResource.hpp"

namespace panda::test
{
namespace
{
auto graphicsQueueFamily(const Engine& engine) -> Result<std::uint32_t>
{
    auto loader = vk::raii::Context {vkGetInstanceProcAddr};
    const auto application = vk::ApplicationInfo {.apiVersion = vk::ApiVersion13};
    const auto createInfo = vk::InstanceCreateInfo {.pApplicationInfo = &application};
    auto createdInstance = panda::core::vulkan::detail::checkedCreation(loader.createInstance(createInfo));
    if (!createdInstance)
    {
        return std::unexpected {createdInstance.error()};
    }
    auto instance = std::move(*createdInstance);
    const auto expectedUuid = engine.getDeviceInfo().deviceUuid;
    auto listedDevices = instance.enumeratePhysicalDevices();
    if (listedDevices.result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(listedDevices.result)};
    }
    auto physicalDevices = std::move(listedDevices.value);
    const auto physicalDevice = std::ranges::find_if(physicalDevices, [&expectedUuid](const auto& device) {
        const auto properties =
            device.template getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceIDProperties>();
        return std::ranges::equal(expectedUuid, properties.template get<vk::PhysicalDeviceIDProperties>().deviceUUID);
    });
    if (physicalDevice == physicalDevices.end())
    {
        return std::unexpected {makeError(ErrorCode::Unsupported, "The Engine's Vulkan device was not found")};
    }
    const auto families = physicalDevice->getQueueFamilyProperties();
    static constexpr auto required = vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute;
    const auto family = std::ranges::find_if(families, [](const auto& properties) {
        return properties.queueCount != 0 && (properties.queueFlags & required) == required;
    });
    if (family == families.end())
    {
        return std::unexpected {makeError(ErrorCode::Unsupported, "The Engine's Vulkan device has no work queue")};
    }
    return static_cast<std::uint32_t>(std::distance(families.begin(), family));
}
}

VulkanBufferReadback::VulkanBufferReadback(Buffer source, Buffer destination)
    : _source {std::move(source)},
      _destination {std::move(destination)},
      _device {detail::BufferResource::getResource(_source)->getNativeDevice()}
{
}

auto VulkanBufferReadback::create(const Engine& context, const Buffer& source)
    -> Result<std::unique_ptr<VulkanBufferReadback>>
{
    auto destination = context.getResourceManager().createBuffer(
        {.size = 8, .usage = ::panda::BufferUsage::TransferDestination, .memory = BufferMemory::Readback});
    if (!destination)
    {
        return std::unexpected {destination.error()};
    }
    auto result = std::unique_ptr<VulkanBufferReadback> {
        new VulkanBufferReadback {source, std::move(*destination)}
    };
    const auto family = graphicsQueueFamily(context);
    if (!family)
    {
        return std::unexpected {family.error()};
    }
    const auto initialized = result->initialize(*family);
    if (!initialized)
    {
        return std::unexpected {initialized.error()};
    }
    return result;
}

auto VulkanBufferReadback::initialize(std::uint32_t family) -> Result<void>
{
    _queue = _device.getQueue(family, 0);
    const auto info = vk::CommandPoolCreateInfo {.queueFamilyIndex = family};
    auto result = _device.createCommandPool(&info, nullptr, &_pool);
    if (result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(result)};
    }
    const auto allocate = vk::CommandBufferAllocateInfo {.commandPool = _pool,
                                                         .level = vk::CommandBufferLevel::ePrimary,
                                                         .commandBufferCount = 2};
    result = _device.allocateCommandBuffers(&allocate, _commands.data());
    if (result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(result)};
    }
    const auto fenceInfo = vk::FenceCreateInfo {};
    for (auto& fence : _fences)
    {
        result = _device.createFence(&fenceInfo, nullptr, &fence);
        if (result != vk::Result::eSuccess)
        {
            return std::unexpected {makeVulkanError(result)};
        }
    }
    return {};
}

VulkanBufferReadback::~VulkanBufferReadback()
{
    const auto finished = wait();
    EXPECT_TRUE(finished);
    for (const auto fence : _fences)
    {
        _device.destroyFence(fence);
    }
    _device.destroyCommandPool(_pool);
}

auto VulkanBufferReadback::record(vk::CommandBuffer command) const -> Result<void>
{
    const auto begin = vk::CommandBufferBeginInfo {.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
    const auto begun = command.begin(&begin);
    if (begun != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(begun)};
    }
    recordCopy(command);
    const auto ended = command.end();
    if (ended != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(ended)};
    }
    return {};
}

void VulkanBufferReadback::recordCopy(vk::CommandBuffer command) const
{
    const auto destination = detail::BufferResource::getResource(_destination)->getNative();
    const auto region = vk::BufferCopy {.dstOffset = _submitted * 4, .size = 4};
    command.copyBuffer(detail::BufferResource::getResource(_source)->getNative(), destination, 1, &region);
    const auto barrier = vk::BufferMemoryBarrier2 {.srcStageMask = vk::PipelineStageFlagBits2::eCopy,
                                                   .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
                                                   .dstStageMask = vk::PipelineStageFlagBits2::eHost,
                                                   .dstAccessMask = vk::AccessFlagBits2::eHostRead,
                                                   .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
                                                   .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
                                                   .buffer = destination,
                                                   .offset = region.dstOffset,
                                                   .size = 4};
    const auto dependency = vk::DependencyInfo {.bufferMemoryBarrierCount = 1, .pBufferMemoryBarriers = &barrier};
    command.pipelineBarrier2(&dependency);
}

auto VulkanBufferReadback::submitCopy() -> Result<void>
{
    if (_submitted == _commands.size())
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Both readback slots are already submitted")};
    }
    const auto command = _commands.at(_submitted);
    const auto recorded = record(command);
    if (!recorded)
    {
        return std::unexpected {recorded.error()};
    }
    const auto commandInfo = vk::CommandBufferSubmitInfo {.commandBuffer = command};
    const auto info = vk::SubmitInfo2 {.commandBufferInfoCount = 1, .pCommandBufferInfos = &commandInfo};
    const auto submitted = _queue.submit2(1, &info, _fences.at(_submitted));
    if (submitted != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(submitted)};
    }
    ++_submitted;
    return {};
}

auto VulkanBufferReadback::wait() const -> Result<void>
{
    if (_submitted != 0)
    {
        const auto result = _device.waitForFences(static_cast<std::uint32_t>(_submitted),
                                                  _fences.data(),
                                                  vk::True,
                                                  std::numeric_limits<std::uint64_t>::max());
        if (result != vk::Result::eSuccess)
        {
            return std::unexpected {makeVulkanError(result)};
        }
    }
    return {};
}

auto VulkanBufferReadback::read() -> Result<std::array<std::byte, 8>>
{
    const auto finished = wait();
    if (!finished)
    {
        return std::unexpected {finished.error()};
    }
    const auto& resource = detail::BufferResource::getResource(_destination);
    const auto invalidated = resource->invalidate(0, 8);
    if (!invalidated)
    {
        return std::unexpected {invalidated.error()};
    }
    const auto mapped = resource->getMappedBytes();
    if (!mapped)
    {
        return std::unexpected {mapped.error()};
    }
    auto result = std::array<std::byte, 8> {};
    std::ranges::copy(mapped->first(8), result.begin());
    return result;
}
}
