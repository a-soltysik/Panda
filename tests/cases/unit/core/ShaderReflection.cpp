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

#include "ScopedMock.hpp"
#include "external/spirv_reflect/SpirvReflectMock.hpp"

namespace
{
using panda::core::vulkan::renderer::reflectShader;
using panda::core::vulkan::renderer::ShaderDescriptorKind;
using panda::core::vulkan::renderer::ShaderStage;
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

    const auto result = reflectShader(shaderWords, ShaderStage::Fragment, "empty fragment");

    ASSERT_TRUE(result) << result.error().message;
    EXPECT_EQ(result->stage, ShaderStage::Fragment);
    EXPECT_TRUE(result->descriptors.empty());
    EXPECT_TRUE(result->pushConstantBlocks.empty());
}
