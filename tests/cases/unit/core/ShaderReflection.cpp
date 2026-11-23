#include "vulkan/renderer/ShaderReflection.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <spirv_reflect.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <panda/base/Error.hpp>
#include <string>
#include <vector>

#include "ScopedMock.hpp"
#include "external/spirv_reflect/SpirvReflectMock.hpp"

namespace
{
using panda::core::vulkan::renderer::buildShaderProgramLayout;
using panda::core::vulkan::renderer::reflectShader;
using panda::core::vulkan::renderer::ShaderBlockMember;
using panda::core::vulkan::renderer::ShaderDescriptor;
using panda::core::vulkan::renderer::ShaderDescriptorKind;
using panda::core::vulkan::renderer::ShaderInterfaceFormat;
using panda::core::vulkan::renderer::ShaderInterfaceVariable;
using panda::core::vulkan::renderer::ShaderProgramLayout;
using panda::core::vulkan::renderer::ShaderReflection;
using panda::core::vulkan::renderer::ShaderSpecializationConstant;
using panda::core::vulkan::renderer::ShaderStage;
using panda::core::vulkan::renderer::validateShaderBlockMembers;
using panda::core::vulkan::renderer::validateShaderProgramDescriptors;
using panda::test::ScopedMock;
using panda::test::SpirvReflectMock;
using ::testing::_;
using ::testing::IsNull;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::StrEq;

constexpr std::array<std::uint32_t, 5> shaderWords {0x07230203U, 0x00010000U, 0U, 1U, 0U};

struct DescriptorCase
{
    std::uint32_t set;
    SpvReflectDescriptorType type;
    ShaderDescriptorKind expectedKind;
};

constexpr auto descriptorCases = std::array {
    DescriptorCase {.set = 0,
                    .type = SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .expectedKind = ShaderDescriptorKind::UniformBuffer                                                                                                   },
    DescriptorCase {.set = 1,
                    .type = SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    .expectedKind = ShaderDescriptorKind::StorageBuffer                                                                                                   },
    DescriptorCase {.set = 2,
                    .type = SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .expectedKind = ShaderDescriptorKind::CombinedImageSampler                                                                                            },
    DescriptorCase {.set = 3,  .type = SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER,                                                    .expectedKind = ShaderDescriptorKind::Other},
    DescriptorCase {
                    .set = 4,  .type = SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE,                                              .expectedKind = ShaderDescriptorKind::Other},
    DescriptorCase {
                    .set = 5,  .type = SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE,                                              .expectedKind = ShaderDescriptorKind::Other},
    DescriptorCase {.set = 6,
                    .type = SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
                    .expectedKind = ShaderDescriptorKind::Other                                                                                                           },
    DescriptorCase {.set = 7,
                    .type = SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
                    .expectedKind = ShaderDescriptorKind::Other                                                                                                           },
    DescriptorCase {.set = 8,
                    .type = SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                    .expectedKind = ShaderDescriptorKind::Other                                                                                                           },
    DescriptorCase {.set = 9,
                    .type = SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,
                    .expectedKind = ShaderDescriptorKind::Other                                                                                                           },
    DescriptorCase {
                    .set = 10, .type = SPV_REFLECT_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,                                           .expectedKind = ShaderDescriptorKind::Other},
    DescriptorCase {.set = 11,
                    .type = SPV_REFLECT_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
                    .expectedKind = ShaderDescriptorKind::Other                                                                                                           },
    DescriptorCase {
                    .set = 12,
                    .type = static_cast<SpvReflectDescriptorType>(SPV_REFLECT_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR + 1),
                    .expectedKind = ShaderDescriptorKind::Other                                                                                                           },
};
using DescriptorBindings = std::array<SpvReflectDescriptorBinding, descriptorCases.size()>;

auto makeDescriptorBindings(SpvReflectBlockVariable& member) -> DescriptorBindings
{
    auto bindings = DescriptorBindings {};
    std::ranges::transform(descriptorCases, bindings.begin(), [&member](const auto& descriptorCase) {
        auto binding = SpvReflectDescriptorBinding {};
        binding.set = descriptorCase.set;
        binding.binding = descriptorCase.set + 1U;
        binding.count = 1;
        binding.descriptor_type = descriptorCase.type;
        binding.block.size = 16;
        binding.block.member_count = 1;
        binding.block.members = &member;
        return binding;
    });
    return bindings;
}

void expectDescriptorReflection(SpirvReflectMock& reflect, DescriptorBindings& bindings)
{
    EXPECT_CALL(reflect, enumerateEntryPointDescriptorBindings(_, StrEq("main"), _, IsNull()))
        .WillOnce([&bindings](const SpvReflectShaderModule*,
                              const char*,
                              std::uint32_t* count,
                              SpvReflectDescriptorBinding**) {
            *count = static_cast<std::uint32_t>(bindings.size());
            return SPV_REFLECT_RESULT_SUCCESS;
        });
    EXPECT_CALL(reflect, enumerateEntryPointDescriptorBindings(_, StrEq("main"), _, NotNull()))
        .WillOnce([&bindings](const SpvReflectShaderModule*,
                              const char*,
                              const std::uint32_t* count,
                              SpvReflectDescriptorBinding** output) {
            if (*count != bindings.size())
            {
                return SPV_REFLECT_RESULT_ERROR_COUNT_MISMATCH;
            }
            std::ranges::transform(bindings, output, [](auto& binding) {
                return &binding;
            });
            return SPV_REFLECT_RESULT_SUCCESS;
        });
    EXPECT_CALL(reflect, enumerateEntryPointPushConstantBlocks(_, StrEq("main"), _, IsNull()))
        .WillOnce([](const SpvReflectShaderModule*, const char*, std::uint32_t* count, SpvReflectBlockVariable**) {
            *count = 0;
            return SPV_REFLECT_RESULT_SUCCESS;
        });
}

class ShaderReflectionUnitTest : public ::testing::Test
{
protected:
    void expectModuleCreated()
    {
        EXPECT_CALL(reflect, createShaderModule(_, _, _))
            .WillOnce([](std::size_t, const void*, SpvReflectShaderModule*) {
                return SPV_REFLECT_RESULT_SUCCESS;
            });
        EXPECT_CALL(reflect, destroyShaderModule(_)).Times(1);
    }

    void expectEmptyInterfaceVariables()
    {
        EXPECT_CALL(reflect, enumerateEntryPointInputVariables(_, StrEq("main"), _, IsNull()))
            .WillOnce(
                [](const SpvReflectShaderModule*, const char*, std::uint32_t* count, SpvReflectInterfaceVariable**) {
                    *count = 0;
                    return SPV_REFLECT_RESULT_SUCCESS;
                });
        EXPECT_CALL(reflect, enumerateEntryPointOutputVariables(_, StrEq("main"), _, IsNull()))
            .WillOnce(
                [](const SpvReflectShaderModule*, const char*, std::uint32_t* count, SpvReflectInterfaceVariable**) {
                    *count = 0;
                    return SPV_REFLECT_RESULT_SUCCESS;
                });
    }

    void expectNoSpecializationConstants()
    {
        EXPECT_CALL(reflect, enumerateSpecializationConstants(_, _, IsNull()))
            .WillOnce([](const SpvReflectShaderModule*, std::uint32_t* count, SpvReflectSpecializationConstant**) {
                *count = 0;
                return SPV_REFLECT_RESULT_SUCCESS;
            });
    }

    void expectNoDescriptorsOrPushConstants()
    {
        EXPECT_CALL(reflect, enumerateEntryPointDescriptorBindings(_, StrEq("main"), _, IsNull()))
            .WillOnce(
                [](const SpvReflectShaderModule*, const char*, std::uint32_t* count, SpvReflectDescriptorBinding**) {
                    *count = 0;
                    return SPV_REFLECT_RESULT_SUCCESS;
                });
        EXPECT_CALL(reflect, enumerateEntryPointPushConstantBlocks(_, StrEq("main"), _, IsNull()))
            .WillOnce([](const SpvReflectShaderModule*, const char*, std::uint32_t* count, SpvReflectBlockVariable**) {
                *count = 0;
                return SPV_REFLECT_RESULT_SUCCESS;
            });
    }

    void expectOneInterfaceInputAndOutput(std::array<SpvReflectInterfaceVariable, 2>& variables)
    {
        EXPECT_CALL(reflect, enumerateEntryPointInputVariables(_, StrEq("main"), _, IsNull()))
            .WillOnce(
                [](const SpvReflectShaderModule*, const char*, std::uint32_t* count, SpvReflectInterfaceVariable**) {
                    *count = 1;
                    return SPV_REFLECT_RESULT_SUCCESS;
                });
        EXPECT_CALL(reflect, enumerateEntryPointInputVariables(_, StrEq("main"), _, NotNull()))
            .WillOnce([&variables](const SpvReflectShaderModule*,
                                   const char*,
                                   const std::uint32_t* count,
                                   SpvReflectInterfaceVariable** inputVariables) {
                EXPECT_EQ(*count, 1U);
                *inputVariables = &variables.front();
                return SPV_REFLECT_RESULT_SUCCESS;
            });
        EXPECT_CALL(reflect, enumerateEntryPointOutputVariables(_, StrEq("main"), _, IsNull()))
            .WillOnce(
                [](const SpvReflectShaderModule*, const char*, std::uint32_t* count, SpvReflectInterfaceVariable**) {
                    *count = 1;
                    return SPV_REFLECT_RESULT_SUCCESS;
                });
        EXPECT_CALL(reflect, enumerateEntryPointOutputVariables(_, StrEq("main"), _, NotNull()))
            .WillOnce([&variables](const SpvReflectShaderModule*,
                                   const char*,
                                   const std::uint32_t* count,
                                   SpvReflectInterfaceVariable** outputVariables) {
                EXPECT_EQ(*count, 1U);
                *outputVariables = &variables.back();
                return SPV_REFLECT_RESULT_SUCCESS;
            });
    }

    void expectEntryPoint(SpvReflectShaderStageFlagBits stage)
    {
        entryPoint.shader_stage = stage;
        reflect.entryPoint = &entryPoint;
    }

    ScopedMock<SpirvReflectMock> reflect;
    // This opaque fake is only read after expectEntryPoint sets its stage.
    // NOLINTNEXTLINE(bugprone-invalid-enum-default-initialization)
    SpvReflectEntryPoint entryPoint {};
};
}

TEST_F(ShaderReflectionUnitTest, MapsModuleAllocationFailure)
{
    EXPECT_CALL(reflect, createShaderModule(_, _, _)).WillOnce(Return(SPV_REFLECT_RESULT_ERROR_ALLOC_FAILED));
    EXPECT_CALL(reflect, destroyShaderModule(_)).Times(0);

    const auto result = reflectShader(shaderWords, ShaderStage::Vertex, "allocation failure");

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, panda::ErrorCode::ResourceExhausted);
    ASSERT_TRUE(result.error().native.has_value());
    EXPECT_EQ(result.error().native->api, "SPIRV-Reflect");
    EXPECT_EQ(result.error().native->code, SPV_REFLECT_RESULT_ERROR_ALLOC_FAILED);
}

TEST_F(ShaderReflectionUnitTest, RejectsMissingMainEntryPoint)
{
    expectModuleCreated();

    const auto result = reflectShader(shaderWords, ShaderStage::Vertex, "missing main");

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, panda::ErrorCode::InvalidArgument);
    EXPECT_NE(result.error().message.find("main"), std::string::npos);
}

TEST_F(ShaderReflectionUnitTest, RejectsUnknownDeclaredStage)
{
    expectModuleCreated();
    expectEntryPoint(SPV_REFLECT_SHADER_STAGE_VERTEX_BIT);

    // Fixed-underlying enums may carry unlisted values, which this branch rejects.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    const auto unknownStage = static_cast<ShaderStage>(std::numeric_limits<std::uint8_t>::max());
    const auto result = reflectShader(shaderWords, unknownStage, "unknown stage");

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, panda::ErrorCode::InvalidArgument);
}

TEST_F(ShaderReflectionUnitTest, RejectsDescriptorCountQueryFailure)
{
    expectModuleCreated();
    expectEntryPoint(SPV_REFLECT_SHADER_STAGE_VERTEX_BIT);
    EXPECT_CALL(reflect, enumerateEntryPointDescriptorBindings(_, StrEq("main"), _, IsNull()))
        .WillOnce(Return(SPV_REFLECT_RESULT_ERROR_ALLOC_FAILED));

    const auto result = reflectShader(shaderWords, ShaderStage::Vertex, "descriptor query failure");

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, panda::ErrorCode::ResourceExhausted);
    ASSERT_TRUE(result.error().native.has_value());
    EXPECT_EQ(result.error().native->code, SPV_REFLECT_RESULT_ERROR_ALLOC_FAILED);
}

TEST_F(ShaderReflectionUnitTest, RejectsDescriptorDataQueryFailure)
{
    expectModuleCreated();
    expectEntryPoint(SPV_REFLECT_SHADER_STAGE_VERTEX_BIT);
    EXPECT_CALL(reflect, enumerateEntryPointDescriptorBindings(_, StrEq("main"), _, IsNull()))
        .WillOnce([](const SpvReflectShaderModule*, const char*, std::uint32_t* count, SpvReflectDescriptorBinding**) {
            *count = 1;
            return SPV_REFLECT_RESULT_SUCCESS;
        });
    EXPECT_CALL(reflect, enumerateEntryPointDescriptorBindings(_, StrEq("main"), _, NotNull()))
        .WillOnce(Return(SPV_REFLECT_RESULT_ERROR_COUNT_MISMATCH));

    const auto result = reflectShader(shaderWords, ShaderStage::Vertex, "descriptor data failure");

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, panda::ErrorCode::InvalidArgument);
    ASSERT_TRUE(result.error().native.has_value());
    EXPECT_EQ(result.error().native->code, SPV_REFLECT_RESULT_ERROR_COUNT_MISMATCH);
}

TEST_F(ShaderReflectionUnitTest, RejectsPushConstantCountQueryFailure)
{
    expectModuleCreated();
    expectEntryPoint(SPV_REFLECT_SHADER_STAGE_VERTEX_BIT);
    EXPECT_CALL(reflect, enumerateEntryPointDescriptorBindings(_, StrEq("main"), _, IsNull()))
        .WillOnce([](const SpvReflectShaderModule*, const char*, std::uint32_t* count, SpvReflectDescriptorBinding**) {
            *count = 0;
            return SPV_REFLECT_RESULT_SUCCESS;
        });
    EXPECT_CALL(reflect, enumerateEntryPointPushConstantBlocks(_, StrEq("main"), _, IsNull()))
        .WillOnce(Return(SPV_REFLECT_RESULT_ERROR_ELEMENT_NOT_FOUND));

    const auto result = reflectShader(shaderWords, ShaderStage::Vertex, "push constant query failure");

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, panda::ErrorCode::InvalidArgument);
    ASSERT_TRUE(result.error().native.has_value());
    EXPECT_EQ(result.error().native->code, SPV_REFLECT_RESULT_ERROR_ELEMENT_NOT_FOUND);
}

TEST_F(ShaderReflectionUnitTest, RejectsPushConstantDataQueryFailure)
{
    expectModuleCreated();
    expectEntryPoint(SPV_REFLECT_SHADER_STAGE_VERTEX_BIT);
    EXPECT_CALL(reflect, enumerateEntryPointDescriptorBindings(_, StrEq("main"), _, IsNull()))
        .WillOnce([](const SpvReflectShaderModule*, const char*, std::uint32_t* count, SpvReflectDescriptorBinding**) {
            *count = 0;
            return SPV_REFLECT_RESULT_SUCCESS;
        });
    EXPECT_CALL(reflect, enumerateEntryPointPushConstantBlocks(_, StrEq("main"), _, IsNull()))
        .WillOnce([](const SpvReflectShaderModule*, const char*, std::uint32_t* count, SpvReflectBlockVariable**) {
            *count = 1;
            return SPV_REFLECT_RESULT_SUCCESS;
        });
    EXPECT_CALL(reflect, enumerateEntryPointPushConstantBlocks(_, StrEq("main"), _, NotNull()))
        .WillOnce(Return(SPV_REFLECT_RESULT_ERROR_COUNT_MISMATCH));

    const auto result = reflectShader(shaderWords, ShaderStage::Vertex, "push constant data failure");

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, panda::ErrorCode::InvalidArgument);
    ASSERT_TRUE(result.error().native.has_value());
    EXPECT_EQ(result.error().native->code, SPV_REFLECT_RESULT_ERROR_COUNT_MISMATCH);
}

TEST_F(ShaderReflectionUnitTest, CopiesDescriptorKindsAndUnnamedMembers)
{
    auto member = SpvReflectBlockVariable {};
    member.offset = 12;
    member.size = 8;
    auto bindings = makeDescriptorBindings(member);

    expectModuleCreated();
    expectEntryPoint(SPV_REFLECT_SHADER_STAGE_VERTEX_BIT);
    expectDescriptorReflection(reflect, bindings);
    expectEmptyInterfaceVariables();
    expectNoSpecializationConstants();

    const auto result = reflectShader(shaderWords, ShaderStage::Vertex, "descriptor kinds");

    ASSERT_TRUE(result) << result.error().message;
    ASSERT_EQ(result->descriptors.size(), descriptorCases.size());
    for (const auto& descriptor : result->descriptors)
    {
        // std::array iterators are pointers in libstdc++ and class objects in MSVC.
        // NOLINTNEXTLINE(readability-qualified-auto)
        const auto expected = std::ranges::find_if(descriptorCases, [&descriptor](const auto& descriptorCase) {
            return descriptorCase.set == descriptor.set;
        });
        ASSERT_NE(expected, descriptorCases.end());
        EXPECT_EQ(descriptor.binding, expected->set + 1U);
        EXPECT_EQ(descriptor.count, 1U);
        EXPECT_EQ(descriptor.kind, expected->expectedKind);
        EXPECT_EQ(descriptor.blockSize, 16U);
        ASSERT_EQ(descriptor.members.size(), 1U);
        EXPECT_TRUE(descriptor.members.front().name.empty());
        EXPECT_EQ(descriptor.members.front().offset, 12U);
        EXPECT_EQ(descriptor.members.front().size, 8U);
    }
}

TEST_F(ShaderReflectionUnitTest, PreservesArrayDimensionsAndRejectsSingleElementDescriptorArrays)
{
    auto member = SpvReflectBlockVariable {};
    auto bindings = makeDescriptorBindings(member);
    bindings.front().array.dims_count = 1;
    bindings.front().array.dims[0] = 1;

    expectModuleCreated();
    expectEntryPoint(SPV_REFLECT_SHADER_STAGE_VERTEX_BIT);
    expectDescriptorReflection(reflect, bindings);
    expectEmptyInterfaceVariables();
    expectNoSpecializationConstants();

    const auto reflection = reflectShader(shaderWords, ShaderStage::Vertex, "descriptor array");

    ASSERT_TRUE(reflection) << reflection.error().message;
    EXPECT_EQ(reflection->descriptors.front().arrayDimensions, (std::vector<std::uint32_t> {1}));
    const auto shaders = std::array {*reflection};
    const auto layout = buildShaderProgramLayout(shaders, "descriptor array");
    ASSERT_FALSE(layout);
    EXPECT_NE(layout.error().message.find("unsupported descriptor"), std::string::npos);
}

TEST_F(ShaderReflectionUnitTest, AcceptsFragmentWithNoDescriptorsOrPushConstants)
{
    expectModuleCreated();
    expectEntryPoint(SPV_REFLECT_SHADER_STAGE_FRAGMENT_BIT);
    EXPECT_CALL(reflect, enumerateEntryPointDescriptorBindings(_, StrEq("main"), _, IsNull()))
        .WillOnce([](const SpvReflectShaderModule*, const char*, std::uint32_t* count, SpvReflectDescriptorBinding**) {
            *count = 0;
            return SPV_REFLECT_RESULT_SUCCESS;
        });
    EXPECT_CALL(reflect, enumerateEntryPointPushConstantBlocks(_, StrEq("main"), _, IsNull()))
        .WillOnce([](const SpvReflectShaderModule*, const char*, std::uint32_t* count, SpvReflectBlockVariable**) {
            *count = 0;
            return SPV_REFLECT_RESULT_SUCCESS;
        });
    expectEmptyInterfaceVariables();
    expectNoSpecializationConstants();

    const auto result = reflectShader(shaderWords, ShaderStage::Fragment, "empty fragment");

    ASSERT_TRUE(result) << result.error().message;
    EXPECT_EQ(result->stage, ShaderStage::Fragment);
    EXPECT_TRUE(result->descriptors.empty());
    EXPECT_TRUE(result->pushConstantBlocks.empty());
    EXPECT_TRUE(result->inputs.empty());
    EXPECT_TRUE(result->outputs.empty());
}

TEST_F(ShaderReflectionUnitTest, CopiesInterfaceVariableNamesLocationsAndFormats)
{
    auto variables = std::array<SpvReflectInterfaceVariable, 2> {};
    auto& input = variables.front();
    input.name = "vertexPosition";
    input.location = 4;
    input.component = std::numeric_limits<std::uint32_t>::max();
    input.format = SPV_REFLECT_FORMAT_R32G32B32_SFLOAT;
    input.built_in = -1;
    auto& output = variables.back();
    output = input;
    output.name = "worldPosition";
    output.location = 2;

    expectModuleCreated();
    expectEntryPoint(SPV_REFLECT_SHADER_STAGE_VERTEX_BIT);
    expectNoDescriptorsOrPushConstants();
    expectNoSpecializationConstants();
    expectOneInterfaceInputAndOutput(variables);

    const auto result = reflectShader(shaderWords, ShaderStage::Vertex, "interface variables");

    ASSERT_TRUE(result) << result.error().message;
    ASSERT_EQ(result->inputs.size(), 1U);
    EXPECT_EQ(
        result->inputs.front(),
        (ShaderInterfaceVariable {.name = "vertexPosition", .location = 4, .format = ShaderInterfaceFormat::Float3}));
    ASSERT_EQ(result->outputs.size(), 1U);
    EXPECT_EQ(result->outputs.front().name, "worldPosition");
    EXPECT_EQ(result->outputs.front().location, 2U);
}

TEST_F(ShaderReflectionUnitTest, RejectsNonzeroInterfaceComponent)
{
    auto input = SpvReflectInterfaceVariable {};
    input.name = "componentInput";
    input.location = 0;
    input.component = 1;
    input.format = SPV_REFLECT_FORMAT_R32_SFLOAT;
    input.built_in = -1;

    expectModuleCreated();
    expectEntryPoint(SPV_REFLECT_SHADER_STAGE_FRAGMENT_BIT);
    expectNoDescriptorsOrPushConstants();
    EXPECT_CALL(reflect, enumerateEntryPointInputVariables(_, StrEq("main"), _, IsNull()))
        .WillOnce([](const SpvReflectShaderModule*, const char*, std::uint32_t* count, SpvReflectInterfaceVariable**) {
            *count = 1;
            return SPV_REFLECT_RESULT_SUCCESS;
        });
    EXPECT_CALL(reflect, enumerateEntryPointInputVariables(_, StrEq("main"), _, NotNull()))
        .WillOnce([&input](const SpvReflectShaderModule*,
                           const char*,
                           const std::uint32_t* count,
                           SpvReflectInterfaceVariable** variables) {
            EXPECT_EQ(*count, 1U);
            *variables = &input;
            return SPV_REFLECT_RESULT_SUCCESS;
        });

    const auto result = reflectShader(shaderWords, ShaderStage::Fragment, "component interface");

    ASSERT_FALSE(result);
    EXPECT_NE(result.error().message.find("component"), std::string::npos);
}

TEST_F(ShaderReflectionUnitTest, CopiesSpecializationConstantNamesAndIds)
{
    auto constant = SpvReflectSpecializationConstant {};
    constant.name = "alphaMode";
    constant.constant_id = 11;

    expectModuleCreated();
    expectEntryPoint(SPV_REFLECT_SHADER_STAGE_FRAGMENT_BIT);
    EXPECT_CALL(reflect, enumerateEntryPointDescriptorBindings(_, StrEq("main"), _, IsNull()))
        .WillOnce([](const SpvReflectShaderModule*, const char*, std::uint32_t* count, SpvReflectDescriptorBinding**) {
            *count = 0;
            return SPV_REFLECT_RESULT_SUCCESS;
        });
    EXPECT_CALL(reflect, enumerateEntryPointPushConstantBlocks(_, StrEq("main"), _, IsNull()))
        .WillOnce([](const SpvReflectShaderModule*, const char*, std::uint32_t* count, SpvReflectBlockVariable**) {
            *count = 0;
            return SPV_REFLECT_RESULT_SUCCESS;
        });
    expectEmptyInterfaceVariables();
    EXPECT_CALL(reflect, enumerateSpecializationConstants(_, _, IsNull()))
        .WillOnce([](const SpvReflectShaderModule*, std::uint32_t* count, SpvReflectSpecializationConstant**) {
            *count = 1;
            return SPV_REFLECT_RESULT_SUCCESS;
        });
    EXPECT_CALL(reflect, enumerateSpecializationConstants(_, _, NotNull()))
        .WillOnce([&constant](const SpvReflectShaderModule*,
                              const std::uint32_t* count,
                              SpvReflectSpecializationConstant** output) {
            EXPECT_EQ(*count, 1U);
            *output = &constant;
            return SPV_REFLECT_RESULT_SUCCESS;
        });

    const auto result = reflectShader(shaderWords, ShaderStage::Fragment, "specialization constant");

    ASSERT_TRUE(result) << result.error().message;
    ASSERT_EQ(result->specializationConstants.size(), 1U);
    EXPECT_EQ(result->specializationConstants.front(), (ShaderSpecializationConstant {.name = "alphaMode", .id = 11}));
}

TEST(ShaderProgramLayout, CombinesMatchingDescriptorsAndPushConstantStages)
{
    const auto member = ShaderBlockMember {.name = "value", .offset = 0, .size = 16, .arrayStride = 0, .members = {}};
    const auto vertex =
        ShaderReflection {.stage = ShaderStage::Vertex,
                          .descriptors = {{.name = "vertexParameters",
                                           .set = 0,
                                           .binding = 3,
                                           .count = 1,
                                           .kind = ShaderDescriptorKind::UniformBuffer,
                                           .arrayDimensions = {},
                                           .blockSize = 16,
                                           .members = {member}}},
                          .pushConstantBlocks = {{.offset = 0, .size = 16, .members = {member}}},
                          .inputs = {{.name = "position", .location = 0, .format = ShaderInterfaceFormat::Float3}},
                          .outputs = {{.name = "tint", .location = 1, .format = ShaderInterfaceFormat::Float4}},
                          .specializationConstants = {}};
    const auto fragment =
        ShaderReflection {.stage = ShaderStage::Fragment,
                          .descriptors = {{.name = "fragmentParameters",
                                           .set = 0,
                                           .binding = 3,
                                           .count = 1,
                                           .kind = ShaderDescriptorKind::UniformBuffer,
                                           .arrayDimensions = {},
                                           .blockSize = 16,
                                           .members = {member}}},
                          .pushConstantBlocks = {},
                          .inputs = {{.name = "tint", .location = 1, .format = ShaderInterfaceFormat::Float4}},
                          .outputs = {},
                          .specializationConstants = {}};
    const auto shaders = std::array {vertex, fragment};

    const auto layout = buildShaderProgramLayout(shaders, "program");

    ASSERT_TRUE(layout) << layout.error().message;
    ASSERT_EQ(layout->descriptors.size(), 1U);
    ASSERT_EQ(layout->descriptors.front().stageLayouts.size(), 2U);
    EXPECT_EQ(layout->descriptors.front().stageLayouts.front().stage, ShaderStage::Vertex);
    EXPECT_EQ(layout->descriptors.front().stageLayouts.back().stage, ShaderStage::Fragment);
    EXPECT_EQ(layout->descriptors.front().names, (std::vector<std::string> {"vertexParameters", "fragmentParameters"}));
    ASSERT_EQ(layout->pushConstantBlocks.size(), 1U);
    EXPECT_EQ(layout->pushConstantBlocks.front().stages, (std::vector {ShaderStage::Vertex}));
    ASSERT_EQ(layout->vertexInputs.size(), 1U);
    EXPECT_EQ(layout->vertexInputs.front().location, 0U);
}

TEST(ShaderProgramLayout, RejectsIncompatibleDescriptorsBeforePipelineCreation)
{
    const auto vertex = ShaderReflection {.stage = ShaderStage::Vertex,
                                          .descriptors = {{.name = {},
                                                           .set = 1,
                                                           .binding = 2,
                                                           .count = 1,
                                                           .kind = ShaderDescriptorKind::UniformBuffer,
                                                           .arrayDimensions = {},
                                                           .blockSize = 16,
                                                           .members = {}}},
                                          .pushConstantBlocks = {},
                                          .inputs = {},
                                          .outputs = {},
                                          .specializationConstants = {}};
    const auto fragment = ShaderReflection {.stage = ShaderStage::Fragment,
                                            .descriptors = {{.name = {},
                                                             .set = 1,
                                                             .binding = 2,
                                                             .count = 1,
                                                             .kind = ShaderDescriptorKind::StorageBuffer,
                                                             .arrayDimensions = {},
                                                             .blockSize = 16,
                                                             .members = {}}},
                                            .pushConstantBlocks = {},
                                            .inputs = {},
                                            .outputs = {},
                                            .specializationConstants = {}};
    const auto shaders = std::array {vertex, fragment};

    const auto layout = buildShaderProgramLayout(shaders, "wrong layout");

    ASSERT_FALSE(layout);
    EXPECT_NE(layout.error().message.find("conflicting descriptors"), std::string::npos);
}

TEST(ShaderProgramLayout, RejectsMultiplePushConstantRangesForOneStage)
{
    auto vertex = ShaderReflection {};
    vertex.stage = ShaderStage::Vertex;
    vertex.pushConstantBlocks = {
        {.offset = 0,  .size = 16, .members = {}},
        {.offset = 32, .size = 16, .members = {}}
    };
    const auto shaders = std::array {vertex};

    const auto layout = buildShaderProgramLayout(shaders, "multiple push constants");

    ASSERT_FALSE(layout);
    EXPECT_NE(layout.error().message.find("more than one push constant range"), std::string::npos);
}

TEST(ShaderBlockLayout, RejectsSwappedSameSizeMembers)
{
    const auto host = std::array {
        ShaderBlockMember {.name = "cameraPosition", .offset = 64, .size = 16, .arrayStride = 0, .members = {}},
        ShaderBlockMember {.name = "ambient",        .offset = 80, .size = 16, .arrayStride = 0, .members = {}}
    };
    const auto shader = std::array {
        ShaderBlockMember {.name = "cameraPosition", .offset = 80, .size = 16, .arrayStride = 0, .members = {}},
        ShaderBlockMember {.name = "ambient",        .offset = 64, .size = 16, .arrayStride = 0, .members = {}}
    };

    const auto result = validateShaderBlockMembers(shader, host, "FrameData");

    ASSERT_FALSE(result);
    EXPECT_NE(result.error().message.find("cameraPosition"), std::string::npos);
}

TEST(ShaderProgramLayout, RejectsDescriptorVariantOutsideSharedLayout)
{
    const auto expected = std::array {
        ShaderDescriptor {.name = "baseColorMap",
                          .set = 1,
                          .binding = 0,
                          .count = 1,
                          .kind = ShaderDescriptorKind::CombinedImageSampler,
                          .arrayDimensions = {},
                          .blockSize = 0,
                          .members = {}}
    };
    auto program = ShaderProgramLayout {};
    program.descriptors.push_back({.set = 1,
                                   .binding = 1,
                                   .count = 1,
                                   .kind = ShaderDescriptorKind::CombinedImageSampler,
                                   .stageLayouts = {},
                                   .names = {"baseColorMap"}});

    const auto result = validateShaderProgramDescriptors(program, expected, "shader variant");

    ASSERT_FALSE(result);
    EXPECT_NE(result.error().message.find("outside the shared layout"), std::string::npos);
}

TEST(ShaderProgramLayout, RejectsFragmentInputsMissingFromVertexOutputs)
{
    const auto vertex = ShaderReflection {.stage = ShaderStage::Vertex,
                                          .descriptors = {},
                                          .pushConstantBlocks = {},
                                          .inputs = {},
                                          .outputs = {},
                                          .specializationConstants = {}};
    const auto fragment =
        ShaderReflection {.stage = ShaderStage::Fragment,
                          .descriptors = {},
                          .pushConstantBlocks = {},
                          .inputs = {{.name = "color", .location = 0, .format = ShaderInterfaceFormat::Float4}},
                          .outputs = {},
                          .specializationConstants = {}};
    const auto shaders = std::array {vertex, fragment};

    const auto layout = buildShaderProgramLayout(shaders, "wrong stage interface");

    ASSERT_FALSE(layout);
    EXPECT_NE(layout.error().message.find("incompatible vertex and fragment interfaces"), std::string::npos);
}

TEST(ShaderProgramLayout, RejectsUnsupportedDescriptorsAndDuplicateStages)
{
    const auto unsupported = ShaderReflection {.stage = ShaderStage::Vertex,
                                               .descriptors = {{.name = {},
                                                                .set = 0,
                                                                .binding = 1,
                                                                .count = 2,
                                                                .kind = ShaderDescriptorKind::Other,
                                                                .arrayDimensions = {},
                                                                .blockSize = 0,
                                                                .members = {}}},
                                               .pushConstantBlocks = {},
                                               .inputs = {},
                                               .outputs = {},
                                               .specializationConstants = {}};
    const auto unsupportedLayout = buildShaderProgramLayout(std::span {&unsupported, 1}, "unsupported");
    ASSERT_FALSE(unsupportedLayout);
    EXPECT_NE(unsupportedLayout.error().message.find("unsupported descriptor"), std::string::npos);

    const auto vertex = ShaderReflection {.stage = ShaderStage::Vertex,
                                          .descriptors = {},
                                          .pushConstantBlocks = {},
                                          .inputs = {},
                                          .outputs = {},
                                          .specializationConstants = {}};
    const auto duplicateStages = std::array {vertex, vertex};
    const auto duplicateLayout = buildShaderProgramLayout(duplicateStages, "duplicate stage");
    ASSERT_FALSE(duplicateLayout);
    EXPECT_NE(duplicateLayout.error().message.find("duplicate shader stage"), std::string::npos);
}
