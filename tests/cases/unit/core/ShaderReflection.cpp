#include "vulkan/renderer/ShaderReflection.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <include/spirv/unified1/spirv.h>
#include <spirv_reflect.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <panda/base/Error.hpp>
#include <panda/rendering/ShaderProgram.hpp>
#include <span>
#include <string>
#include <vector>

#include "ScopedMock.hpp"
#include "external/spirv_reflect/SpirvReflectMock.hpp"

namespace
{
using panda::core::vulkan::renderer::buildShaderProgramLayout;
using panda::core::vulkan::renderer::reflectGraphicsProgram;
using panda::core::vulkan::renderer::reflectShader;
using panda::core::vulkan::renderer::ShaderDescriptorKind;
using panda::core::vulkan::renderer::ShaderInterfaceFormat;
using panda::core::vulkan::renderer::ShaderInterfaceVariable;
using panda::core::vulkan::renderer::ShaderSpecializationConstant;
using panda::core::vulkan::renderer::ShaderStage;
using panda::test::ScopedMock;
using panda::test::SpirvReflectMock;
using ::testing::_;
using ::testing::IsNull;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::StrEq;

constexpr std::array<std::uint32_t, 5> shaderWords {0x07230203U, 0x00010000U, 0U, 1U, 0U};

auto makeEntryPoint(SpvReflectShaderStageFlagBits stage) -> SpvReflectEntryPoint
{
    return {
        .name = nullptr,
        .id = 0,
        .spirv_execution_model = static_cast<decltype(SpvReflectEntryPoint::spirv_execution_model)>(0),
        .shader_stage = stage,
        .input_variable_count = 0,
        .input_variables = nullptr,
        .output_variable_count = 0,
        .output_variables = nullptr,
        .interface_variable_count = 0,
        .interface_variables = nullptr,
        .descriptor_set_count = 0,
        .descriptor_sets = nullptr,
        .used_uniform_count = 0,
        .used_uniforms = nullptr,
        .used_push_constant_count = 0,
        .used_push_constants = nullptr,
        .execution_mode_count = 0,
        .execution_modes = nullptr,
        .local_size = {.x = 0, .y = 0, .z = 0},
        .invocations = 0,
        .output_vertices = 0
    };
}

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

auto makeSampledImageType() -> SpvReflectTypeDescription
{
    auto type = SpvReflectTypeDescription {};
    type.type_flags = SPV_REFLECT_TYPE_FLAG_FLOAT;
    type.traits.numeric.scalar.width = 32;
    return type;
}

auto makeDescriptorBindings(SpvReflectBlockVariable& member, SpvReflectTypeDescription& imageType) -> DescriptorBindings
{
    auto bindings = DescriptorBindings {};
    std::ranges::transform(descriptorCases, bindings.begin(), [&member, &imageType](const auto& descriptorCase) {
        auto binding = SpvReflectDescriptorBinding {};
        binding.set = descriptorCase.set;
        binding.binding = descriptorCase.set + 1U;
        binding.count = 1;
        binding.descriptor_type = descriptorCase.type;
        binding.decoration_flags = SPV_REFLECT_DECORATION_NON_WRITABLE;
        binding.image.dim = SpvDim2D;
        binding.type_description = &imageType;
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

    void expectOneInput(SpvReflectInterfaceVariable& input)
    {
        EXPECT_CALL(reflect, enumerateEntryPointInputVariables(_, StrEq("main"), _, IsNull()))
            .WillOnce(
                [](const SpvReflectShaderModule*, const char*, std::uint32_t* count, SpvReflectInterfaceVariable**) {
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
        EXPECT_CALL(this->reflect, enumerateEntryPointDescriptorBindings(_, StrEq("main"), _, IsNull()))
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

TEST_F(ShaderReflectionUnitTest, PropagatesSpecializationConstantEnumerationFailure)
{
    expectModuleCreated();
    expectEntryPoint(SPV_REFLECT_SHADER_STAGE_VERTEX_BIT);
    expectNoDescriptorsOrPushConstants();
    expectEmptyInterfaceVariables();
    EXPECT_CALL(reflect, enumerateSpecializationConstants(_, _, IsNull()))
        .WillOnce(Return(SPV_REFLECT_RESULT_ERROR_ALLOC_FAILED));

    const auto result = reflectShader(shaderWords, ShaderStage::Vertex, "specialization constants");

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, panda::ErrorCode::ResourceExhausted);
    ASSERT_TRUE(result.error().native.has_value());
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

namespace
{
enum class EnumerationFailure : std::uint8_t
{
    DescriptorCount,
    DescriptorData,
    PushConstantCount,
    PushConstantData
};

struct EnumerationFailureCase
{
    EnumerationFailure failure;
    SpvReflectResult nativeResult;
    panda::ErrorCode errorCode;
    const char* name;
};

class ShaderEnumerationFailureTest : public ShaderReflectionUnitTest,
                                     public ::testing::WithParamInterface<EnumerationFailureCase>
{
protected:
    void expectFailure()
    {
        switch (GetParam().failure)
        {
        case EnumerationFailure::DescriptorCount:
            expectDescriptorCountFailure();
            break;
        case EnumerationFailure::DescriptorData:
            expectDescriptorDataFailure();
            break;
        case EnumerationFailure::PushConstantCount:
            expectPushConstantCountFailure();
            break;
        case EnumerationFailure::PushConstantData:
            expectPushConstantDataFailure();
            break;
        }
    }

    void expectDescriptorCountFailure()
    {
        EXPECT_CALL(reflect, enumerateEntryPointDescriptorBindings(_, StrEq("main"), _, IsNull()))
            .WillOnce(testing::Return(SPV_REFLECT_RESULT_ERROR_ALLOC_FAILED));
    }

    void expectDescriptorDataFailure()
    {
        EXPECT_CALL(reflect, enumerateEntryPointDescriptorBindings(_, StrEq("main"), _, IsNull()))
            .WillOnce(
                [](const SpvReflectShaderModule*, const char*, std::uint32_t* count, SpvReflectDescriptorBinding**) {
                    *count = 1;
                    return SPV_REFLECT_RESULT_SUCCESS;
                });
        EXPECT_CALL(reflect, enumerateEntryPointDescriptorBindings(_, StrEq("main"), _, NotNull()))
            .WillOnce(testing::Return(SPV_REFLECT_RESULT_ERROR_COUNT_MISMATCH));
    }

    void expectPushConstantCountFailure()
    {
        expectNoDescriptorBindings();
        EXPECT_CALL(reflect, enumerateEntryPointPushConstantBlocks(_, StrEq("main"), _, IsNull()))
            .WillOnce(testing::Return(SPV_REFLECT_RESULT_ERROR_ELEMENT_NOT_FOUND));
    }

    void expectPushConstantDataFailure()
    {
        expectNoDescriptorBindings();
        EXPECT_CALL(reflect, enumerateEntryPointPushConstantBlocks(_, StrEq("main"), _, IsNull()))
            .WillOnce([](const SpvReflectShaderModule*, const char*, std::uint32_t* count, SpvReflectBlockVariable**) {
                *count = 1;
                return SPV_REFLECT_RESULT_SUCCESS;
            });
        EXPECT_CALL(reflect, enumerateEntryPointPushConstantBlocks(_, StrEq("main"), _, NotNull()))
            .WillOnce(testing::Return(SPV_REFLECT_RESULT_ERROR_COUNT_MISMATCH));
    }

    void expectNoDescriptorBindings()
    {
        EXPECT_CALL(reflect, enumerateEntryPointDescriptorBindings(_, StrEq("main"), _, IsNull()))
            .WillOnce(
                [](const SpvReflectShaderModule*, const char*, std::uint32_t* count, SpvReflectDescriptorBinding**) {
                    *count = 0;
                    return SPV_REFLECT_RESULT_SUCCESS;
                });
    }
};
}

TEST_P(ShaderEnumerationFailureTest, ReportsCountAndDataQueryFailures)
{
    const auto& testCase = GetParam();
    expectModuleCreated();
    expectEntryPoint(SPV_REFLECT_SHADER_STAGE_VERTEX_BIT);
    expectFailure();

    const auto result = reflectShader(shaderWords, ShaderStage::Vertex, "enumeration failure");

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, testCase.errorCode);
    ASSERT_TRUE(result.error().native.has_value());
    EXPECT_EQ(result.error().native->code, testCase.nativeResult);
}

INSTANTIATE_TEST_SUITE_P(
    DescriptorAndPushConstant,
    ShaderEnumerationFailureTest,
    ::testing::Values(EnumerationFailureCase {.failure = EnumerationFailure::DescriptorCount,
                                              .nativeResult = SPV_REFLECT_RESULT_ERROR_ALLOC_FAILED,
                                              .errorCode = panda::ErrorCode::ResourceExhausted,
                                              .name = "DescriptorCount"},
                      EnumerationFailureCase {.failure = EnumerationFailure::DescriptorData,
                                              .nativeResult = SPV_REFLECT_RESULT_ERROR_COUNT_MISMATCH,
                                              .errorCode = panda::ErrorCode::InvalidArgument,
                                              .name = "DescriptorData"},
                      EnumerationFailureCase {.failure = EnumerationFailure::PushConstantCount,
                                              .nativeResult = SPV_REFLECT_RESULT_ERROR_ELEMENT_NOT_FOUND,
                                              .errorCode = panda::ErrorCode::InvalidArgument,
                                              .name = "PushConstantCount"},
                      EnumerationFailureCase {.failure = EnumerationFailure::PushConstantData,
                                              .nativeResult = SPV_REFLECT_RESULT_ERROR_COUNT_MISMATCH,
                                              .errorCode = panda::ErrorCode::InvalidArgument,
                                              .name = "PushConstantData"}),
    [](const ::testing::TestParamInfo<EnumerationFailureCase>& testInfo) {
        return testInfo.param.name;
    });

TEST_F(ShaderReflectionUnitTest, CopiesDescriptorKindsAndUnnamedMembers)
{
    auto member = SpvReflectBlockVariable {};
    member.offset = 12;
    member.size = 8;
    auto imageType = makeSampledImageType();
    auto bindings = makeDescriptorBindings(member, imageType);

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
    auto imageType = makeSampledImageType();
    auto bindings = makeDescriptorBindings(member, imageType);
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

TEST_F(ShaderReflectionUnitTest, CopiesMatrixColumnCountFromInterfaceVariable)
{
    auto variables = std::array<SpvReflectInterfaceVariable, 2> {};
    auto& input = variables.front();
    input.name = "instanceModelMatrix";
    input.location = 5;
    input.component = std::numeric_limits<std::uint32_t>::max();
    input.format = SPV_REFLECT_FORMAT_R32G32B32A32_SFLOAT;
    input.numeric.matrix.column_count = 4;
    input.numeric.matrix.row_count = 4;
    input.built_in = -1;
    auto& output = variables.back();
    output = input;
    output.name = "worldPosition";
    output.location = 0;

    expectModuleCreated();
    expectEntryPoint(SPV_REFLECT_SHADER_STAGE_VERTEX_BIT);
    expectNoDescriptorsOrPushConstants();
    expectNoSpecializationConstants();
    expectOneInterfaceInputAndOutput(variables);

    const auto result = reflectShader(shaderWords, ShaderStage::Vertex, "matrix interface");

    ASSERT_TRUE(result) << result.error().message;
    ASSERT_EQ(result->inputs.size(), 1U);
    EXPECT_EQ(result->inputs.front(),
              (ShaderInterfaceVariable {.name = "instanceModelMatrix",
                                        .location = 5,
                                        .format = ShaderInterfaceFormat::Float4,
                                        .matrixColumns = 4}));
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

TEST_F(ShaderReflectionUnitTest, MapsSignedIntegerInterfaceFormat)
{
    auto input = SpvReflectInterfaceVariable {};
    input.name = "integerInput";
    input.location = 0;
    input.format = SPV_REFLECT_FORMAT_R32_SINT;
    input.built_in = -1;

    expectModuleCreated();
    expectEntryPoint(SPV_REFLECT_SHADER_STAGE_VERTEX_BIT);
    expectNoDescriptorsOrPushConstants();
    expectOneInput(input);
    EXPECT_CALL(reflect, enumerateEntryPointOutputVariables(_, StrEq("main"), _, IsNull()))
        .WillOnce([](const SpvReflectShaderModule*, const char*, std::uint32_t* count, SpvReflectInterfaceVariable**) {
            *count = 0;
            return SPV_REFLECT_RESULT_SUCCESS;
        });
    expectNoSpecializationConstants();

    const auto result = reflectShader(shaderWords, ShaderStage::Vertex, "signed input");

    ASSERT_TRUE(result) << result.error().message;
    ASSERT_EQ(result->inputs.size(), 1U);
    EXPECT_EQ(result->inputs.front().format, ShaderInterfaceFormat::SignedInteger);
}

TEST_F(ShaderReflectionUnitTest, RejectsInterfaceWithoutLocation)
{
    auto input = SpvReflectInterfaceVariable {};
    input.name = "missingLocation";
    input.location = std::numeric_limits<std::uint32_t>::max();
    input.format = SPV_REFLECT_FORMAT_R32_SFLOAT;
    input.built_in = -1;

    expectModuleCreated();
    expectEntryPoint(SPV_REFLECT_SHADER_STAGE_VERTEX_BIT);
    expectNoDescriptorsOrPushConstants();
    expectOneInput(input);

    const auto result = reflectShader(shaderWords, ShaderStage::Vertex, "missing interface location");

    ASSERT_FALSE(result);
    EXPECT_NE(result.error().message.find("without a location"), std::string::npos);
}

TEST_F(ShaderReflectionUnitTest, RejectsUnsupportedInterfaceFormat)
{
    auto input = SpvReflectInterfaceVariable {};
    input.name = "unsupportedFormat";
    input.location = 0;
    input.format = SPV_REFLECT_FORMAT_R64_SFLOAT;
    input.built_in = -1;

    expectModuleCreated();
    expectEntryPoint(SPV_REFLECT_SHADER_STAGE_VERTEX_BIT);
    expectNoDescriptorsOrPushConstants();
    expectOneInput(input);

    const auto result = reflectShader(shaderWords, ShaderStage::Vertex, "unsupported interface format");

    ASSERT_FALSE(result);
    EXPECT_NE(result.error().message.find("unsupported interface format"), std::string::npos);
}

TEST_F(ShaderReflectionUnitTest, RejectsInvalidPushConstantRange)
{
    auto block = SpvReflectBlockVariable {};
    block.offset = 8;
    block.size = 4;

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
        .WillOnce([&block](const SpvReflectShaderModule*,
                           const char*,
                           const std::uint32_t* count,
                           SpvReflectBlockVariable** blocks) {
            EXPECT_EQ(*count, 1U);
            *blocks = &block;
            return SPV_REFLECT_RESULT_SUCCESS;
        });

    const auto result = reflectShader(shaderWords, ShaderStage::Vertex, "invalid push constant");

    ASSERT_FALSE(result);
    EXPECT_NE(result.error().message.find("invalid push constant range"), std::string::npos);
}

TEST_F(ShaderReflectionUnitTest, RejectsInterfaceEnumerationFailure)
{
    expectModuleCreated();
    expectEntryPoint(SPV_REFLECT_SHADER_STAGE_VERTEX_BIT);
    expectNoDescriptorsOrPushConstants();
    EXPECT_CALL(reflect, enumerateEntryPointInputVariables(_, StrEq("main"), _, IsNull()))
        .WillOnce(Return(SPV_REFLECT_RESULT_ERROR_ALLOC_FAILED));

    const auto result = reflectShader(shaderWords, ShaderStage::Vertex, "interface query failure");

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, panda::ErrorCode::ResourceExhausted);
}

TEST_F(ShaderReflectionUnitTest, RejectsGraphicsProgramStageReflectionFailures)
{
    expectModuleCreated();
    expectEntryPoint(SPV_REFLECT_SHADER_STAGE_FRAGMENT_BIT);
    const auto vertexFailure = reflectGraphicsProgram(shaderWords, std::nullopt, "vertex failure");
    ASSERT_FALSE(vertexFailure);

    auto vertexEntryPoint = makeEntryPoint(SPV_REFLECT_SHADER_STAGE_VERTEX_BIT);
    auto fragmentEntryPoint = makeEntryPoint(SPV_REFLECT_SHADER_STAGE_VERTEX_BIT);
    EXPECT_CALL(reflect, createShaderModule(_, _, _))
        .WillOnce([this, &vertexEntryPoint](std::size_t, const void*, SpvReflectShaderModule*) {
            reflect.entryPoint = &vertexEntryPoint;
            return SPV_REFLECT_RESULT_SUCCESS;
        })
        .WillOnce([this, &fragmentEntryPoint](std::size_t, const void*, SpvReflectShaderModule*) {
            reflect.entryPoint = &fragmentEntryPoint;
            return SPV_REFLECT_RESULT_SUCCESS;
        });
    EXPECT_CALL(reflect, destroyShaderModule(_)).Times(2);
    expectNoDescriptorsOrPushConstants();
    expectEmptyInterfaceVariables();
    expectNoSpecializationConstants();
    const auto fragmentFailure =
        reflectGraphicsProgram(shaderWords, std::span<const std::uint32_t> {shaderWords}, "fragment failure");

    EXPECT_FALSE(fragmentFailure);
}
