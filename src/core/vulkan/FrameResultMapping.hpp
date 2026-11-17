#pragma once

// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <panda/diagnostics/Assert.hpp>
#include <panda/rendering/FrameResult.hpp>

namespace panda::core::vulkan
{
[[nodiscard]] inline auto mapSampleCount(vk::SampleCountFlagBits sampleCount) -> panda::SampleCount
{
    switch (sampleCount)
    {
    case vk::SampleCountFlagBits::e1:
        return panda::SampleCount::One;
    case vk::SampleCountFlagBits::e2:
        return panda::SampleCount::Two;
    case vk::SampleCountFlagBits::e4:
        return panda::SampleCount::Four;
    case vk::SampleCountFlagBits::e8:
    case vk::SampleCountFlagBits::e16:
    case vk::SampleCountFlagBits::e32:
    case vk::SampleCountFlagBits::e64:
    default:
        panda::panic("Renderer selected an unsupported public sample count");
    }
}
}
