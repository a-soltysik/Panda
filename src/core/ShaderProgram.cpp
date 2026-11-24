#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <expected>
#include <format>
#include <memory>
#include <optional>
#include <panda/base/Error.hpp>
#include <panda/rendering/ShaderProgram.hpp>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "vulkan/renderer/FrameDescriptors.hpp"
#include "vulkan/renderer/ShaderProgramState.hpp"
#include "vulkan/renderer/ShaderReflection.hpp"

namespace panda
{
namespace renderer = core::vulkan::renderer;

namespace
{
using renderer::OwnedShaderCode;
using renderer::ProgramVariant;
using renderer::ShaderProgramDescriptor;
using renderer::ShaderProgramPushConstantBlock;
using renderer::ShadowVariant;

auto sameStageLayout(const ShaderProgramDescriptor::StageLayout& first,
                     const ShaderProgramDescriptor::StageLayout& second) -> bool
{
    return first.stage == second.stage && first.blockSize == second.blockSize && first.members == second.members;
}

auto sameDescriptor(const ShaderProgramDescriptor& first, const ShaderProgramDescriptor& second) -> bool
{
    return first.set == second.set && first.binding == second.binding && first.count == second.count &&
           first.kind == second.kind && first.stageLayouts.size() == second.stageLayouts.size() &&
           std::ranges::equal(first.stageLayouts, second.stageLayouts, sameStageLayout) &&
           first.names == second.names && first.sampledImage == second.sampledImage;
}

auto samePushConstant(const ShaderProgramPushConstantBlock& first, const ShaderProgramPushConstantBlock& second) -> bool
{
    return first.block.offset == second.block.offset && first.block.size == second.block.size &&
           first.block.members == second.block.members && first.stages == second.stages;
}

auto sameProgramInterface(const renderer::ShaderProgramLayout& first, const renderer::ShaderProgramLayout& second)
    -> bool
{
    return first.descriptors.size() == second.descriptors.size() &&
           std::ranges::equal(first.descriptors, second.descriptors, sameDescriptor) &&
           first.pushConstantBlocks.size() == second.pushConstantBlocks.size() &&
           std::ranges::equal(first.pushConstantBlocks, second.pushConstantBlocks, samePushConstant) &&
           first.vertexInputs == second.vertexInputs && first.specializationConstants == second.specializationConstants;
}

auto sameShadowInterface(const ShadowVariant& first, const ShadowVariant& second) -> bool
{
    if (const auto* firstProgram = std::get_if<ProgramVariant>(&first); firstProgram != nullptr)
    {
        const auto* secondProgram = std::get_if<ProgramVariant>(&second);
        return secondProgram != nullptr && sameProgramInterface(firstProgram->layout, secondProgram->layout);
    }
    const auto* firstMode = std::get_if<ShaderShadowMode>(&first);
    const auto* secondMode = std::get_if<ShaderShadowMode>(&second);
    return firstMode != nullptr && secondMode != nullptr && *firstMode == *secondMode;
}

auto nextProgramRevision() -> std::uint64_t
{
    static auto revision = std::atomic<std::uint64_t> {1};
    return revision.fetch_add(1, std::memory_order_relaxed);
}

auto copyShaderCode(ShaderCode code) -> OwnedShaderCode
{
    return {.words = std::vector<std::uint32_t>(code.words.begin(), code.words.end()),
            .label = std::string {code.label}};
}

auto reflectStage(const ShaderCode& code, ShaderStage expectedStage, std::string_view variant)
    -> Result<renderer::ShaderReflection>
{
    if (code.stage != expectedStage)
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument,
                      std::format("{} shader '{}' is in the wrong stage slot", variant, code.label))};
    }
    return renderer::reflectShader(code.words, expectedStage, std::format("{}: {}", variant, code.label));
}

auto reflectVariant(const ShaderCode& vertex, const std::optional<ShaderCode>& fragment, std::string_view label)
    -> Result<renderer::ShaderProgramLayout>
{
    return reflectStage(vertex, ShaderStage::Vertex, label).and_then([&](auto reflectedVertex) {
        if (!fragment)
        {
            return renderer::buildShaderProgramLayout(std::span {&reflectedVertex, 1}, label);
        }
        return reflectStage(*fragment, ShaderStage::Fragment, label).and_then([&](auto reflectedFragment) {
            const auto stages = std::array {std::move(reflectedVertex), std::move(reflectedFragment)};
            return renderer::buildShaderProgramLayout(stages, label);
        });
    });
}

auto loadVariant(const ShaderCode& vertex,
                 const std::optional<ShaderCode>& fragment,
                 ShaderVertexInput vertexInput,
                 std::string_view label) -> Result<ProgramVariant>
{
    return reflectVariant(vertex, fragment, label).and_then([&](auto layout) -> Result<ProgramVariant> {
        if (vertexInput == ShaderVertexInput::Procedural && !layout.vertexInputs.empty())
        {
            return std::unexpected {makeError(ErrorCode::InvalidArgument,
                                              std::format("{} procedural shader declares mesh vertex inputs", label))};
        }
        return ProgramVariant {.vertex = copyShaderCode(vertex),
                               .fragment = fragment.transform(copyShaderCode),
                               .layout = std::move(layout)};
    });
}

auto loadShadowVariant(const ProgramDesc& description) -> Result<ShadowVariant>
{
    if (const auto* shadow = std::get_if<ShaderShadowStages>(&description.shadow); shadow != nullptr)
    {
        return loadVariant(shadow->vertex, shadow->fragment, description.vertexInput, "Shadow program")
            .transform([](auto variant) -> ShadowVariant {
                return std::move(variant);
            });
    }
    const auto* mode = std::get_if<ShaderShadowMode>(&description.shadow);
    if (mode != nullptr &&
        (*mode == ShaderShadowMode::None ||
         (*mode == ShaderShadowMode::StandardMesh && description.vertexInput == ShaderVertexInput::StandardMesh)))
    {
        return *mode;
    }
    return std::unexpected {makeError(ErrorCode::InvalidArgument, "Shader program has an incompatible shadow mode")};
}
}

auto renderer::sameShaderProgramInterface(const ProgramVariant& firstCamera,
                                          const ShadowVariant& firstShadow,
                                          const ProgramVariant& secondCamera,
                                          const ShadowVariant& secondShadow) -> bool
{
    return sameProgramInterface(firstCamera.layout, secondCamera.layout) &&
           sameShadowInterface(firstShadow, secondShadow);
}

auto ShaderProgram::create(const vk::raii::Device& device, const ProgramDesc& description) -> Result<ShaderProgram>
{
    if (description.vertexInput != ShaderVertexInput::StandardMesh &&
        description.vertexInput != ShaderVertexInput::Procedural)
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Shader program has an invalid vertex input contract")};
    }
    auto camera = loadVariant(description.vertex, description.fragment, description.vertexInput, "Camera program");
    if (!camera)
    {
        return std::unexpected {std::move(camera.error())};
    }
    auto shadow = loadShadowVariant(description);
    if (!shadow)
    {
        return std::unexpected {std::move(shadow.error())};
    }
    const auto* explicitShadow = std::get_if<ProgramVariant>(&*shadow);
    const auto* shadowLayout = explicitShadow == nullptr ? nullptr : &explicitShadow->layout;
    if (auto valid = renderer::validateCustomShaderLayouts(camera->layout, shadowLayout); !valid)
    {
        return std::unexpected {std::move(valid.error())};
    }
    return ShaderProgram {std::make_shared<State>(State {.revision = nextProgramRevision(),
                                                         .device = &device,
                                                         .vertexInput = description.vertexInput,
                                                         .camera = std::move(*camera),
                                                         .shadow = std::move(*shadow)})};
}

ShaderProgram::ShaderProgram(std::shared_ptr<const State> state)
    : _identity {std::make_shared<Identity>()}
{
    _identity->current.store(std::move(state), std::memory_order_release);
}

auto ShaderProgram::snapshot() const -> std::shared_ptr<const State>
{
    return _identity ? _identity->current.load(std::memory_order_acquire) : nullptr;
}

auto ShaderProgram::isValid() const noexcept -> bool
{
    return snapshot() != nullptr;
}
}
