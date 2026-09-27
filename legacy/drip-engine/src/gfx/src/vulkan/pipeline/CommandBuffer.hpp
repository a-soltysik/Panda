#pragma once
// clang-format off
#include <drip/common/utils/Assert.hpp>
// clang-format on

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_handles.hpp>

#include "vulkan/core/Device.hpp"

namespace drip::gfx
{

class CommandBuffer
{
public:
    [[nodiscard]] static auto beginSingleTimeCommandBuffer(const Device& device) noexcept -> vk::CommandBuffer;
    static void endSingleTimeCommandBuffer(const Device& device, vk::CommandBuffer buffer) noexcept;
};

}
