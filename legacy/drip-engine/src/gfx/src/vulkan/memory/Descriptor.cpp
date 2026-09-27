// clang-format off
#include <drip/common/utils/Assert.hpp>
// clang-format on

#include "vulkan/memory/Descriptor.hpp"

#include <cstdint>
#include <memory>
#include <ranges>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_structs.hpp>

#include "drip/gfx/utils/format/ResultFormatter.hpp"  //NOLINT(misc-include-cleaner)
#include "vulkan/core/Device.hpp"

namespace drip::gfx
{

DescriptorSetLayout::Builder::Builder(const Device& device)
    : _device {device}
{
}

auto DescriptorSetLayout::Builder::addBinding(uint32_t binding,
                                              vk::DescriptorType descriptorType,
                                              vk::ShaderStageFlags stageFlags,
                                              uint32_t count) -> Builder&
{
    common::ExpectNot(_bindings.contains(binding), "Binding: {} already in use", binding);
    const auto layoutBinding = vk::DescriptorSetLayoutBinding {.binding = binding,
                                                               .descriptorType = descriptorType,
                                                               .descriptorCount = count,
                                                               .stageFlags = stageFlags};
    _bindings.insert({binding, layoutBinding});

    return *this;
}

auto DescriptorSetLayout::Builder::build(vk::DescriptorSetLayoutCreateFlags flags) const
    -> std::unique_ptr<DescriptorSetLayout>
{
    return std::make_unique<DescriptorSetLayout>(_device, _bindings, flags);
}

DescriptorSetLayout::DescriptorSetLayout(const Device& device,
                                         const std::unordered_map<uint32_t, vk::DescriptorSetLayoutBinding>& bindings,
                                         vk::DescriptorSetLayoutCreateFlags flags)
    : _bindings {bindings},
      _device {device},
      _descriptorSetLayout {createDescriptorSetLayout(device, bindings, flags)}
{
}

DescriptorSetLayout::~DescriptorSetLayout() noexcept
{
    _device.logicalDevice.destroyDescriptorSetLayout(_descriptorSetLayout);
}

auto DescriptorSetLayout::createDescriptorSetLayout(
    const Device& device,
    const std::unordered_map<uint32_t, vk::DescriptorSetLayoutBinding>& bindings,
    vk::DescriptorSetLayoutCreateFlags flags) -> vk::DescriptorSetLayout
{
    const auto layoutBindings =
        std::ranges::to<std::vector<vk::DescriptorSetLayoutBinding>>(bindings | std::ranges::views::values);

    const auto descriptorSetLayoutInfo =
        vk::DescriptorSetLayoutCreateInfo {.flags = flags,
                                           .bindingCount = static_cast<uint32_t>(layoutBindings.size()),
                                           .pBindings = layoutBindings.data()};
    return common::Expect(device.logicalDevice.createDescriptorSetLayout(descriptorSetLayoutInfo),
                          vk::Result::eSuccess,
                          "Failed to create descriptor set layout")
        .result();
}

auto DescriptorSetLayout::getDescriptorSetLayoutBinding(uint32_t binding) const -> const vk::DescriptorSetLayoutBinding&
{
    return common::ExpectNot(_bindings.find(binding), _bindings.cend(), "Binding: {} does not exist", binding)
        .result()
        ->second;
}

auto DescriptorSetLayout::getDescriptorSetLayout() const noexcept -> vk::DescriptorSetLayout
{
    return _descriptorSetLayout;
}

DescriptorPool::Builder::Builder(const Device& device)
    : _device {device}
{
}

auto DescriptorPool::Builder::addPoolSize(vk::DescriptorType descriptorType, uint32_t count) -> Builder&
{
    _poolSizes.emplace_back(descriptorType, count);
    return *this;
}

auto DescriptorPool::Builder::build(uint32_t maxSets, vk::DescriptorPoolCreateFlags flags)
    -> std::unique_ptr<DescriptorPool>
{
    return std::make_unique<DescriptorPool>(_device, flags, maxSets, _poolSizes);
}

DescriptorPool::DescriptorPool(const Device& device,
                               vk::DescriptorPoolCreateFlags poolFlags,
                               uint32_t maxSets,
                               const std::vector<vk::DescriptorPoolSize>& poolSizes)
    : _device {device},
      _descriptorPool {createDescriptorPool(device, maxSets, poolFlags, poolSizes)}
{
}

DescriptorPool::~DescriptorPool() noexcept
{
    _device.logicalDevice.destroyDescriptorPool(_descriptorPool);
}

auto DescriptorPool::allocateDescriptor(vk::DescriptorSetLayout descriptorSetLayout,
                                        vk::DescriptorSet& descriptor) const -> bool
{
    const auto allocInfo = vk::DescriptorSetAllocateInfo {.descriptorPool = _descriptorPool,
                                                          .descriptorSetCount = 1,
                                                          .pSetLayouts = &descriptorSetLayout};

    return common::ShouldBe(_device.logicalDevice.allocateDescriptorSets(&allocInfo, &descriptor),
                            vk::Result::eSuccess,
                            "Failed to allocate descriptor sets")
        .result();
}

auto DescriptorPool::createDescriptorPool(const Device& device,
                                          uint32_t maxSets,
                                          vk::DescriptorPoolCreateFlags poolFlags,
                                          const std::vector<vk::DescriptorPoolSize>& poolSizes) -> vk::DescriptorPool
{
    const auto descriptorPoolInfo =
        vk::DescriptorPoolCreateInfo {.flags = poolFlags,
                                      .maxSets = maxSets,
                                      .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
                                      .pPoolSizes = poolSizes.data()};

    return common::Expect(device.logicalDevice.createDescriptorPool(descriptorPoolInfo),
                          vk::Result::eSuccess,
                          "Failed to create descriptor pool!")
        .result();
}

void DescriptorPool::freeDescriptors(const std::vector<vk::DescriptorSet>& descriptors) const
{
    common::Expect(_device.logicalDevice.freeDescriptorSets(_descriptorPool, descriptors),
                   vk::Result::eSuccess,
                   "Failed to free descriptor sets");
}

void DescriptorPool::resetPool() const
{
    common::Expect(_device.logicalDevice.resetDescriptorPool(_descriptorPool),
                   vk::Result::eSuccess,
                   "Failed to reset descriptor pool");
}

auto DescriptorPool::getHandle() const noexcept -> vk::DescriptorPool
{
    return _descriptorPool;
}

DescriptorWriter::DescriptorWriter(const DescriptorSetLayout& setLayout)
    : _setLayout {setLayout}
{
}

auto DescriptorWriter::writeBuffer(uint32_t binding, const vk::DescriptorBufferInfo& bufferInfo) -> DescriptorWriter&
{
    const auto& bindingDescription = _setLayout.getDescriptorSetLayoutBinding(binding);

    _writes.push_back(vk::WriteDescriptorSet {
        .dstBinding = binding,
        .descriptorCount = 1,
        .descriptorType = bindingDescription.descriptorType,
        .pBufferInfo = &bufferInfo,
    });
    return *this;
}

auto DescriptorWriter::writeImage(uint32_t binding, const vk::DescriptorImageInfo& imageInfo) -> DescriptorWriter&
{
    const auto& bindingDescription = _setLayout.getDescriptorSetLayoutBinding(binding);

    _writes.push_back(vk::WriteDescriptorSet {
        .dstBinding = binding,
        .descriptorCount = 1,
        .descriptorType = bindingDescription.descriptorType,
        .pImageInfo = &imageInfo,
    });
    return *this;
}

void DescriptorWriter::push(vk::CommandBuffer commandBuffer, vk::PipelineLayout layout) const
{
    commandBuffer.pushDescriptorSetKHR(vk::PipelineBindPoint::eGraphics, layout, 0, _writes);
}

}
