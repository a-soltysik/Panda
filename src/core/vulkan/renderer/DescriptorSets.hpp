#pragma once
#include <cstddef>
#include <panda/base/Error.hpp>
#include <span>
#include <vector>

#include "VulkanHpp.hpp"

namespace panda::core::vulkan::renderer
{
struct DescriptorSets
{
    vk::raii::DescriptorPool pool {nullptr};
    std::vector<vk::raii::DescriptorSet> sets;
};

[[nodiscard]] auto createDescriptorSets(const vk::raii::Device& device,
                                        vk::DescriptorSetLayout layout,
                                        std::span<const vk::DescriptorSetLayoutBinding> bindings,
                                        std::size_t count) -> Result<DescriptorSets>;
}
