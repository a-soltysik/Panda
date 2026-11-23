#pragma once

#include <cstdint>
#include <map>
#include <panda/rendering/RenderView.hpp>
#include <string>
#include <string_view>
#include <vector>

#include "renderer/DescriptorSets.hpp"
#include "renderer/ShaderReflection.hpp"

namespace panda::core::vulkan::renderer
{
struct FrameBufferDescriptors;

struct ReflectedDescriptorBinding
{
    std::uint32_t set {};
    vk::DescriptorSetLayoutBinding layout;
};

class FrameDescriptors final
{
public:
    explicit FrameDescriptors(const vk::raii::Device& device);

    [[nodiscard]] auto initialize(const vk::PhysicalDeviceLimits& limits, std::size_t frames) -> Result<void>;
    [[nodiscard]] auto initializeShadowSampler() -> Result<void>;
    [[nodiscard]] auto initializeEnvironmentSampler() -> Result<void>;
    [[nodiscard]] auto getPipelineLayout() const noexcept -> vk::PipelineLayout;
    [[nodiscard]] auto getFrameDescriptorSetIndex() const noexcept -> std::uint32_t;
    [[nodiscard]] auto getTextureDescriptorSetIndex() const noexcept -> std::uint32_t;
    [[nodiscard]] auto getFrameDescriptorSet(std::size_t index) const -> vk::DescriptorSet;
    [[nodiscard]] auto getShaderDescriptorSchema() const noexcept -> const std::vector<ShaderDescriptor>&;

    [[nodiscard]] auto textureLayout() const noexcept -> vk::DescriptorSetLayout;

    [[nodiscard]] auto materialTextureBindings() const -> std::vector<vk::DescriptorSetLayoutBinding>;
    void write(std::size_t index,
               const FrameBufferDescriptors& buffers,
               vk::ImageView shadowView,
               const RenderView& view);

private:
    void writeEnvironment(vk::DescriptorSet descriptorSet, const RenderView& view);
    [[nodiscard]] auto reflectShaderBindings(std::uint32_t maxSets) -> Result<void>;
    [[nodiscard]] auto createLayout(std::uint32_t set) const -> Result<vk::DescriptorSetLayout>;
    [[nodiscard]] auto descriptorSetLayouts() -> Result<std::vector<vk::DescriptorSetLayout>>;
    [[nodiscard]] auto initializePipelineLayout() -> Result<void>;
    [[nodiscard]] auto shaderBinding(std::string_view name) const -> const ReflectedDescriptorBinding&;

    const vk::raii::Device& _device;
    vk::raii::DescriptorSetLayout _descriptorSetLayout {nullptr};
    vk::raii::DescriptorSetLayout _textureDescriptorSetLayout {nullptr};
    std::vector<vk::raii::DescriptorSetLayout> _emptyDescriptorSetLayouts;
    vk::raii::PipelineLayout _pipelineLayout {nullptr};
    std::map<std::string, ReflectedDescriptorBinding, std::less<>> _shaderBindings;
    std::vector<ShaderDescriptor> _shaderDescriptorSchema;
    vk::PushConstantRange _pushConstantRange;
    std::uint32_t _frameDescriptorSetIndex {};
    std::uint32_t _textureDescriptorSetIndex {};
    vk::raii::Sampler _shadowSampler {nullptr};
    vk::raii::Sampler _environmentSampler {nullptr};
    DescriptorSets _descriptors;
};
}
