#pragma once

#include <cstdint>
#include <optional>
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

// NOLINTBEGIN(misc-no-recursion) This models a finite reflected block tree by value.
struct ShaderBlockMember
{
    std::string name;
    std::uint32_t offset {};
    std::uint32_t size {};
    std::uint32_t arrayStride {};
    std::vector<ShaderBlockMember> members;
    friend auto operator==(const ShaderBlockMember&, const ShaderBlockMember&) -> bool = default;
};
// NOLINTEND(misc-no-recursion)

enum class ShaderInterfaceFormat : std::uint8_t
{
    Float,
    SignedInteger,
    UnsignedInteger,
    Float2,
    Float3,
    Float4
};

struct ShaderInterfaceVariable
{
    std::string name;
    std::uint32_t location {};
    ShaderInterfaceFormat format {ShaderInterfaceFormat::Float};
    friend auto operator==(const ShaderInterfaceVariable&, const ShaderInterfaceVariable&) -> bool = default;
};

struct ShaderSpecializationConstant
{
    std::string name;
    std::uint32_t id {};
    friend auto operator==(const ShaderSpecializationConstant&, const ShaderSpecializationConstant&) -> bool = default;
};

struct ShaderDescriptor
{
    std::string name;
    std::uint32_t set {};
    std::uint32_t binding {};
    std::uint32_t count {};
    ShaderDescriptorKind kind {ShaderDescriptorKind::Other};
    std::vector<std::uint32_t> arrayDimensions;
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
    std::vector<ShaderInterfaceVariable> inputs;
    std::vector<ShaderInterfaceVariable> outputs;
    std::vector<ShaderSpecializationConstant> specializationConstants;
};

struct ShaderProgramDescriptor
{
    std::uint32_t set {};
    std::uint32_t binding {};
    std::uint32_t count {};
    ShaderDescriptorKind kind {ShaderDescriptorKind::Other};

    struct StageLayout
    {
        ShaderStage stage {ShaderStage::Vertex};
        std::uint32_t blockSize {};
        std::vector<ShaderBlockMember> members;
    };

    std::vector<StageLayout> stageLayouts;
    std::vector<std::string> names;
};

struct ShaderProgramPushConstantBlock
{
    ShaderPushConstantBlock block;
    std::vector<ShaderStage> stages;
};

struct ShaderProgramSpecializationConstant
{
    std::string name;
    std::uint32_t id {};
    ShaderStage stage {ShaderStage::Vertex};
    friend auto operator==(const ShaderProgramSpecializationConstant&, const ShaderProgramSpecializationConstant&)
        -> bool = default;
};

struct ShaderProgramLayout
{
    std::vector<ShaderProgramDescriptor> descriptors;
    std::vector<ShaderProgramPushConstantBlock> pushConstantBlocks;
    std::vector<ShaderInterfaceVariable> vertexInputs;
    std::vector<ShaderProgramSpecializationConstant> specializationConstants;
};

[[nodiscard]] auto reflectShader(std::span<const std::uint32_t> words,
                                 ShaderStage expectedStage,
                                 std::string_view diagnosticLabel) -> Result<ShaderReflection>;
[[nodiscard]] auto reflectGraphicsProgram(std::span<const std::uint32_t> vertexWords,
                                          std::optional<std::span<const std::uint32_t>> fragmentWords,
                                          std::string_view diagnosticLabel) -> Result<ShaderProgramLayout>;
[[nodiscard]] auto buildShaderProgramLayout(std::span<const ShaderReflection> shaders, std::string_view diagnosticLabel)
    -> Result<ShaderProgramLayout>;
[[nodiscard]] auto specializationConstantId(const ShaderProgramLayout& program,
                                            ShaderStage stage,
                                            std::string_view name,
                                            std::string_view diagnosticLabel) -> Result<std::uint32_t>;
[[nodiscard]] auto validateShaderBlockMembers(std::span<const ShaderBlockMember> shaderMembers,
                                              std::span<const ShaderBlockMember> hostMembers,
                                              std::string_view diagnosticLabel,
                                              bool requireComplete = false) -> Result<void>;
[[nodiscard]] auto validateShaderProgramDescriptors(const ShaderProgramLayout& program,
                                                    std::span<const ShaderDescriptor> expectedDescriptors,
                                                    std::string_view diagnosticLabel) -> Result<void>;
}
