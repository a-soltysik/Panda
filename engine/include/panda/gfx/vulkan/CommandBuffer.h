#pragma once

#include "Device.h"
#include "panda/Common.h"

namespace panda::gfx::vulkan
{

class CommandBuffer
{
public:
    [[nodiscard]] static auto beginSingleTimeCommandBuffer(const Device& device) noexcept -> vk::CommandBuffer;
    static auto endSingleTimeCommandBuffer(const Device& device, vk::CommandBuffer buffer) noexcept -> void;
};

}