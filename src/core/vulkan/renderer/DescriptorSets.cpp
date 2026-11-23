// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include "renderer/DescriptorSets.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <panda/base/Error.hpp>
#include <panda/vulkan/Error.hpp>
#include <span>
#include <utility>
#include <vector>

namespace panda::core::vulkan::renderer
{
namespace
{
auto allocateDescriptorSets(const vk::raii::Device& device,
                            vk::DescriptorPool pool,
                            vk::DescriptorSetLayout layout,
                            std::uint32_t setCount) -> std::expected<std::vector<vk::raii::DescriptorSet>, Error>
{
    const auto layouts = std::vector(setCount, layout);
    const auto allocation = vk::DescriptorSetAllocateInfo {.descriptorPool = pool,
                                                           .descriptorSetCount = setCount,
                                                           .pSetLayouts = layouts.data()};
    auto sets = device.allocateDescriptorSets(allocation);
    if (sets.result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(sets.result)};
    }
    return std::move(sets.value);
}

auto descriptorPoolSizes(std::span<const vk::DescriptorSetLayoutBinding> bindings, std::uint32_t setCount)
    -> Result<std::vector<vk::DescriptorPoolSize>>
{
    auto sizes = std::vector<vk::DescriptorPoolSize> {};
    for (const auto& binding : bindings)
    {
        const auto count = static_cast<std::uint64_t>(setCount) * binding.descriptorCount;
        if (count > std::numeric_limits<std::uint32_t>::max())
        {
            return std::unexpected {
                makeError(ErrorCode::ResourceExhausted, "Descriptor pool size exceeds Vulkan limits")};
        }
        const auto existing = std::ranges::find(sizes, binding.descriptorType, &vk::DescriptorPoolSize::type);
        if (existing == sizes.end())
        {
            sizes.push_back({.type = binding.descriptorType, .descriptorCount = static_cast<std::uint32_t>(count)});
        }
        else if (existing->descriptorCount > std::numeric_limits<std::uint32_t>::max() - count)
        {
            return std::unexpected {
                makeError(ErrorCode::ResourceExhausted, "Descriptor pool size exceeds Vulkan limits")};
        }
        else
        {
            existing->descriptorCount += static_cast<std::uint32_t>(count);
        }
    }
    return sizes;
}

}

auto createDescriptorSets(const vk::raii::Device& device,
                          vk::DescriptorSetLayout layout,
                          std::span<const vk::DescriptorSetLayoutBinding> bindings,
                          std::size_t count) -> std::expected<DescriptorSets, Error>
{
    if (count > std::numeric_limits<std::uint32_t>::max())
    {
        return std::unexpected {makeError(ErrorCode::ResourceExhausted, "Descriptor set count exceeds Vulkan limits")};
    }
    const auto setCount = static_cast<std::uint32_t>(count);
    const auto poolSizes = descriptorPoolSizes(bindings, setCount);
    if (!poolSizes)
    {
        return std::unexpected {poolSizes.error()};
    }
    const auto poolInfo = vk::DescriptorPoolCreateInfo {.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
                                                        .maxSets = setCount,
                                                        .poolSizeCount = static_cast<std::uint32_t>(poolSizes->size()),
                                                        .pPoolSizes = poolSizes->data()};
    auto nativePool = vk::DescriptorPool {};
    const auto created = (*device).createDescriptorPool(&poolInfo, nullptr, &nativePool, *device.getDispatcher());
    if (created != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(created)};
    }
    auto pool = vk::raii::DescriptorPool {device, nativePool};
    return allocateDescriptorSets(device, *pool, layout, setCount).transform([&](auto sets) {
        return DescriptorSets {.pool = std::move(pool), .sets = std::move(sets)};
    });
}
}
