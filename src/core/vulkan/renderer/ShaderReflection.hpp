#pragma once

#include <cstdint>
#include <panda/base/Error.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace panda::core::vulkan::renderer
{
enum class ShaderStage : std::uint8_t
{
    Vertex,
    Fragment
};

enum class ShaderDescriptorKind : std::uint8_t
{
    UniformBuffer,
    StorageBuffer,
    CombinedImageSampler,
    Other
};

struct ShaderBlockMember
{
    std::string name;
    std::uint32_t offset {};
    std::uint32_t size {};
};

struct ShaderDescriptor
{
    std::uint32_t set {};
    std::uint32_t binding {};
    std::uint32_t count {};
    ShaderDescriptorKind kind {ShaderDescriptorKind::Other};
    std::uint32_t blockSize {};
    std::vector<ShaderBlockMember> members;
};

struct ShaderPushConstantBlock
{
    std::uint32_t offset {};
    std::uint32_t size {};
    std::vector<ShaderBlockMember> members;
};

struct ShaderReflection
{
    ShaderStage stage {ShaderStage::Vertex};
    std::vector<ShaderDescriptor> descriptors;
    std::vector<ShaderPushConstantBlock> pushConstantBlocks;
};

[[nodiscard]] auto reflectShader(std::span<const std::uint32_t> words,
                                 ShaderStage expectedStage,
                                 std::string_view diagnosticLabel) -> Result<ShaderReflection>;
}
