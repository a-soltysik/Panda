#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <panda/rendering/ShaderProgram.hpp>
#include <string>
#include <variant>
#include <vector>

#include "renderer/ShaderReflection.hpp"

namespace panda::core::vulkan::renderer
{
struct OwnedShaderCode
{
    std::vector<std::uint32_t> words;
    std::string label;
};

struct ProgramVariant
{
    OwnedShaderCode vertex;
    std::optional<OwnedShaderCode> fragment;
    ShaderProgramLayout layout;
};

using ShadowVariant = std::variant<ShaderShadowMode, ProgramVariant>;

[[nodiscard]] auto sameShaderProgramInterface(const ProgramVariant& firstCamera,
                                              const ShadowVariant& firstShadow,
                                              const ProgramVariant& secondCamera,
                                              const ShadowVariant& secondShadow) -> bool;
}

namespace panda
{
struct ShaderProgram::State
{
    std::uint64_t revision {};
    const vk::raii::Device* device {};
    ShaderVertexInput vertexInput {ShaderVertexInput::StandardMesh};
    core::vulkan::renderer::ProgramVariant camera;
    core::vulkan::renderer::ShadowVariant shadow;
};

struct ShaderProgram::Identity
{
    std::shared_ptr<const State> current;
};
}
