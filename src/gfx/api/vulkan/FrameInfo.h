#pragma once

#include "gfx/Camera.h"

namespace panda::gfx::vulkan
{

struct FrameInfo
{
    Camera& camera;
    vk::CommandBuffer commandBuffer;
    vk::DescriptorSet descriptorSet;

    uint32_t frameIndex;
    float deltaTime;
};

}
