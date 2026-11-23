// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan.hpp>
// clang-format on

#include "vulkan/renderer/ShaderReflection.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <spirv_reflect.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <panda/base/Error.hpp>
#include <span>
#include <string>
#include <tuple>
#include <vector>

#include "FakeHandle.hpp"
#include "ScopedMock.hpp"
#include "external/spirv_reflect/SpirvReflectMock.hpp"
#include "external/vulkan/VulkanMock.hpp"
#include "presentation/ToneMappingPass.hpp"

namespace
{
using panda::core::vulkan::renderer::buildShaderProgramLayout;
using panda::core::vulkan::renderer::reflectGraphicsProgram;
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
using panda::core::vulkan::renderer::specializationConstantId;
using panda::core::vulkan::renderer::validateShaderBlockMembers;
using panda::core::vulkan::renderer::validateShaderProgramDescriptors;
using panda::test::fakeHandle;
using panda::test::ScopedMock;
using panda::test::SpirvReflectMock;
using ::testing::_;
using ::testing::AnyNumber;
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

class ToneMappingPassReflectionTest : public ::testing::Test
{
protected:
    void TearDown() override
    {
        std::ignore = device.release();
        std::ignore = instance.release();
    }

    void expectShaderReflection(std::optional<std::uint32_t> descriptorSet)
    {
        descriptor = SpvReflectDescriptorBinding {};
        descriptor.name = "hdrColor";
        descriptor.set = descriptorSet.value_or(0U);
        descriptor.binding = 0;
        descriptor.count = 1;
        descriptor.descriptor_type = SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        specialization = SpvReflectSpecializationConstant {};
        specialization.name = "outputIsSrgb";
        specialization.constant_id = 7;
        moduleIndex = 0;
        expectShaderModules();
        expectDescriptorBindings(descriptorSet);
        expectNoPushConstants();
        expectEmptyInterfaceVariables();
        expectSpecializationConstants();
    }

    void expectShaderModules()
    {
        EXPECT_CALL(reflect, createShaderModule(_, _, _))
            .Times(2)
            .WillRepeatedly([this](std::size_t, const void*, SpvReflectShaderModule*) {
                entryPoint.shader_stage =
                    moduleIndex++ == 0 ? SPV_REFLECT_SHADER_STAGE_VERTEX_BIT : SPV_REFLECT_SHADER_STAGE_FRAGMENT_BIT;
                reflect.entryPoint = &entryPoint;
                return SPV_REFLECT_RESULT_SUCCESS;
            });
        EXPECT_CALL(reflect, destroyShaderModule(_)).Times(2);
    }

    void expectDescriptorBindings(std::optional<std::uint32_t> descriptorSet)
    {
        EXPECT_CALL(reflect, enumerateEntryPointDescriptorBindings(_, StrEq("main"), _, IsNull()))
            .Times(2)
            .WillOnce(
                [](const SpvReflectShaderModule*, const char*, std::uint32_t* count, SpvReflectDescriptorBinding**) {
                    *count = 0;
                    return SPV_REFLECT_RESULT_SUCCESS;
                })
            .WillOnce([descriptorSet](const SpvReflectShaderModule*,
                                      const char*,
                                      std::uint32_t* count,
                                      SpvReflectDescriptorBinding**) {
                *count = descriptorSet ? 1U : 0U;
                return SPV_REFLECT_RESULT_SUCCESS;
            });
        if (descriptorSet)
        {
            EXPECT_CALL(this->reflect, enumerateEntryPointDescriptorBindings(_, StrEq("main"), _, NotNull()))
                .WillOnce([this](const SpvReflectShaderModule*,
                                 const char*,
                                 const std::uint32_t* count,
                                 SpvReflectDescriptorBinding** bindings) {
                    EXPECT_EQ(*count, 1U);
                    *bindings = &this->descriptor;
                    return SPV_REFLECT_RESULT_SUCCESS;
                });
        }
    }

    void expectNoPushConstants()
    {
        EXPECT_CALL(reflect, enumerateEntryPointPushConstantBlocks(_, StrEq("main"), _, IsNull()))
            .Times(2)
            .WillRepeatedly(
                [](const SpvReflectShaderModule*, const char*, std::uint32_t* count, SpvReflectBlockVariable**) {
                    *count = 0;
                    return SPV_REFLECT_RESULT_SUCCESS;
                });
    }

    void expectEmptyInterfaceVariables()
    {
        EXPECT_CALL(reflect, enumerateEntryPointInputVariables(_, StrEq("main"), _, IsNull()))
            .Times(2)
            .WillRepeatedly(
                [](const SpvReflectShaderModule*, const char*, std::uint32_t* count, SpvReflectInterfaceVariable**) {
                    *count = 0;
                    return SPV_REFLECT_RESULT_SUCCESS;
                });
        EXPECT_CALL(reflect, enumerateEntryPointOutputVariables(_, StrEq("main"), _, IsNull()))
            .Times(2)
            .WillRepeatedly(
                [](const SpvReflectShaderModule*, const char*, std::uint32_t* count, SpvReflectInterfaceVariable**) {
                    *count = 0;
                    return SPV_REFLECT_RESULT_SUCCESS;
                });
    }

    void expectSpecializationConstants()
    {
        EXPECT_CALL(reflect, enumerateSpecializationConstants(_, _, IsNull()))
            .Times(2)
            .WillOnce([](const SpvReflectShaderModule*, std::uint32_t* count, SpvReflectSpecializationConstant**) {
                *count = 0;
                return SPV_REFLECT_RESULT_SUCCESS;
            })
            .WillOnce([](const SpvReflectShaderModule*, std::uint32_t* count, SpvReflectSpecializationConstant**) {
                *count = 1;
                return SPV_REFLECT_RESULT_SUCCESS;
            });
        EXPECT_CALL(reflect, enumerateSpecializationConstants(_, _, NotNull()))
            .WillOnce([this](const SpvReflectShaderModule*,
                             const std::uint32_t* count,
                             SpvReflectSpecializationConstant** constants) {
                EXPECT_EQ(*count, 1U);
                *constants = &specialization;
                return SPV_REFLECT_RESULT_SUCCESS;
            });
    }

    void expectSuccessfulInitialization()
    {
        expectDescriptorLayouts();
        expectPipelineLayoutAndSampler();
        expectDescriptorPoolAndSets();
    }

    void expectDescriptorLayouts()
    {
        EXPECT_CALL(vulkan, vkCreateDescriptorSetLayout(deviceHandle, NotNull(), nullptr, NotNull()))
            .Times(2)
            .WillOnce([this](VkDevice,
                             const VkDescriptorSetLayoutCreateInfo* info,
                             const VkAllocationCallbacks*,
                             VkDescriptorSetLayout* output) {
                EXPECT_EQ(info->bindingCount, 1U);
                *output = fakeHandle<VkDescriptorSetLayout>(nextHandle++);
                return static_cast<VkResult>(vk::Result::eSuccess);
            })
            .WillOnce([this](VkDevice,
                             const VkDescriptorSetLayoutCreateInfo* info,
                             const VkAllocationCallbacks*,
                             VkDescriptorSetLayout* output) {
                EXPECT_EQ(info->bindingCount, 0U);
                *output = fakeHandle<VkDescriptorSetLayout>(nextHandle++);
                return static_cast<VkResult>(vk::Result::eSuccess);
            });
        EXPECT_CALL(vulkan, vkDestroyDescriptorSetLayout(deviceHandle, _, nullptr)).Times(2);
    }

    void expectPipelineLayoutAndSampler()
    {
        EXPECT_CALL(vulkan, vkCreatePipelineLayout(deviceHandle, NotNull(), nullptr, NotNull()))
            .WillOnce([this](VkDevice,
                             const VkPipelineLayoutCreateInfo* info,
                             const VkAllocationCallbacks*,
                             VkPipelineLayout* output) {
                EXPECT_EQ(info->setLayoutCount, 2U);
                *output = fakeHandle<VkPipelineLayout>(nextHandle++);
                return static_cast<VkResult>(vk::Result::eSuccess);
            });
        EXPECT_CALL(vulkan, vkDestroyPipelineLayout(deviceHandle, _, nullptr));
        EXPECT_CALL(vulkan, vkCreateSampler(deviceHandle, NotNull(), nullptr, NotNull()))
            .WillOnce([this](VkDevice, const VkSamplerCreateInfo*, const VkAllocationCallbacks*, VkSampler* output) {
                *output = fakeHandle<VkSampler>(nextHandle++);
                return static_cast<VkResult>(vk::Result::eSuccess);
            });
        EXPECT_CALL(vulkan, vkDestroySampler(deviceHandle, _, nullptr));
    }

    void expectDescriptorPoolAndSets()
    {
        EXPECT_CALL(vulkan, vkCreateDescriptorPool(deviceHandle, NotNull(), nullptr, NotNull()))
            .WillOnce([this](VkDevice,
                             const VkDescriptorPoolCreateInfo*,
                             const VkAllocationCallbacks*,
                             VkDescriptorPool* output) {
                *output = fakeHandle<VkDescriptorPool>(nextHandle++);
                return static_cast<VkResult>(vk::Result::eSuccess);
            });
        EXPECT_CALL(vulkan, vkDestroyDescriptorPool(deviceHandle, _, nullptr));
        EXPECT_CALL(vulkan, vkAllocateDescriptorSets(deviceHandle, NotNull(), NotNull()))
            .WillOnce([this](VkDevice, const VkDescriptorSetAllocateInfo* info, VkDescriptorSet* output) {
                for (auto& set : std::span {output, info->descriptorSetCount})
                {
                    set = fakeHandle<VkDescriptorSet>(nextHandle++);
                }
                return static_cast<VkResult>(vk::Result::eSuccess);
            });
        EXPECT_CALL(vulkan, vkFreeDescriptorSets(deviceHandle, _, _, _))
            .Times(AnyNumber())
            .WillRepeatedly(Return(static_cast<VkResult>(vk::Result::eSuccess)));
    }

    ScopedMock<SpirvReflectMock> reflect;
    ScopedMock<panda::test::VulkanMock> vulkan;
    vk::raii::Context loader {vkGetInstanceProcAddr};
    vk::raii::Instance instance {loader, fakeHandle<VkInstance>(1)};
    vk::raii::PhysicalDevice physical {instance, fakeHandle<VkPhysicalDevice>(2)};
    VkDevice deviceHandle {fakeHandle<VkDevice>(3)};
    vk::raii::Device device {physical, deviceHandle};
    std::uintptr_t nextHandle {10};
    SpvReflectEntryPoint entryPoint {makeEntryPoint(SPV_REFLECT_SHADER_STAGE_VERTEX_BIT)};
    SpvReflectDescriptorBinding descriptor {};
    SpvReflectSpecializationConstant specialization {};
    std::size_t moduleIndex {};
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

TEST(ShaderProgramLayout, RejectsEmptyAndFragmentOnlyPrograms)
{
    const auto empty = buildShaderProgramLayout(std::span<const ShaderReflection> {}, "empty program");
    ASSERT_FALSE(empty);
    EXPECT_NE(empty.error().message.find("no shader stages"), std::string::npos);

    const auto fragment = ShaderReflection {.stage = ShaderStage::Fragment,
                                            .descriptors = {},
                                            .pushConstantBlocks = {},
                                            .inputs = {},
                                            .outputs = {},
                                            .specializationConstants = {}};
    const auto fragmentOnly = buildShaderProgramLayout(std::span {&fragment, 1}, "fragment-only program");
    ASSERT_FALSE(fragmentOnly);
    EXPECT_NE(fragmentOnly.error().message.find("must include a vertex stage"), std::string::npos);
}

TEST(ShaderProgramLayout, RejectsDuplicateInterfaceLocations)
{
    auto vertex = ShaderReflection {};
    vertex.inputs = {
        {.name = "first",  .location = 0, .format = ShaderInterfaceFormat::Float},
        {.name = "second", .location = 0, .format = ShaderInterfaceFormat::Float}
    };
    const auto duplicateInputs = buildShaderProgramLayout(std::span {&vertex, 1}, "duplicate vertex inputs");
    ASSERT_FALSE(duplicateInputs);
    EXPECT_NE(duplicateInputs.error().message.find("duplicate vertex interface locations"), std::string::npos);

    vertex.inputs.clear();
    const auto fragment = ShaderReflection {
        .stage = ShaderStage::Fragment,
        .descriptors = {},
        .pushConstantBlocks = {},
        .inputs = {{.name = "first", .location = 0, .format = ShaderInterfaceFormat::Float},
                        {.name = "second", .location = 0, .format = ShaderInterfaceFormat::Float}},
        .outputs = {},
        .specializationConstants = {}
    };
    const auto duplicateFragmentInputs = std::array {vertex, fragment};
    const auto duplicateFragmentLayout = buildShaderProgramLayout(duplicateFragmentInputs, "duplicate fragment inputs");
    ASSERT_FALSE(duplicateFragmentLayout);
    EXPECT_NE(duplicateFragmentLayout.error().message.find("duplicate fragment input locations"), std::string::npos);
}

TEST(ShaderProgramLayout, RejectsDuplicateSpecializationConstants)
{
    auto vertex = ShaderReflection {};
    vertex.specializationConstants = {
        {.name = "first",  .id = 1},
        {.name = "second", .id = 1}
    };

    const auto result = buildShaderProgramLayout(std::span {&vertex, 1}, "duplicate specialization constants");

    ASSERT_FALSE(result);
    EXPECT_NE(result.error().message.find("duplicate specialization constants"), std::string::npos);
}

TEST(ShaderProgramLayout, RejectsInvalidAndConflictingPushConstantLayouts)
{
    auto vertex = ShaderReflection {};
    vertex.pushConstantBlocks = {
        {.offset = 0, .size = 0, .members = {}}
    };
    auto invalid = buildShaderProgramLayout(std::span {&vertex, 1}, "empty push constants");
    ASSERT_FALSE(invalid);
    EXPECT_NE(invalid.error().message.find("invalid push constant range"), std::string::npos);

    vertex.pushConstantBlocks = {
        {.offset = 0,
         .size = 16,
         .members = {{.name = "vertexValue", .offset = 0, .size = 16, .arrayStride = 0, .members = {}}}}
    };
    auto fragment = ShaderReflection {
        .stage = ShaderStage::Fragment,
        .descriptors = {},
        .pushConstantBlocks =
            {{.offset = 0,
              .size = 16,
              .members = {{.name = "fragmentValue", .offset = 0, .size = 16, .arrayStride = 0, .members = {}}}}},
        .inputs = {},
        .outputs = {},
        .specializationConstants = {}};
    const auto stages = std::array {vertex, fragment};
    const auto conflict = buildShaderProgramLayout(stages, "conflicting push constants");
    ASSERT_FALSE(conflict);
    EXPECT_NE(conflict.error().message.find("conflicting push constant layouts"), std::string::npos);
}

TEST(ShaderProgramLayout, RejectsMissingSpecializationConstant)
{
    const auto program = ShaderProgramLayout {};
    const auto result = specializationConstantId(program, ShaderStage::Fragment, "alphaMode", "mesh pipeline");

    ASSERT_FALSE(result);
    EXPECT_NE(result.error().message.find("alphaMode"), std::string::npos);
}

TEST(ShaderBlockLayout, RejectsIncompleteNestedBlocksAndUnnamedDescriptors)
{
    const auto shaderMembers = std::array {
        ShaderBlockMember {.name = "transform", .offset = 0, .size = 16, .arrayStride = 0, .members = {}}
    };
    const auto hostMembers = std::array {
        ShaderBlockMember {.name = "transform",
                           .offset = 0,
                           .size = 16,
                           .arrayStride = 0,
                           .members = {{.name = "value", .offset = 0, .size = 16, .arrayStride = 0, .members = {}}}}
    };
    const auto block = validateShaderBlockMembers(shaderMembers, hostMembers, "nested block", true);
    ASSERT_FALSE(block);
    EXPECT_NE(block.error().message.find("incomplete shader block"), std::string::npos);

    auto program = ShaderProgramLayout {};
    program.descriptors.push_back({.set = 0,
                                   .binding = 0,
                                   .count = 1,
                                   .kind = ShaderDescriptorKind::UniformBuffer,
                                   .stageLayouts = {},
                                   .names = {}});
    const auto descriptor = validateShaderProgramDescriptors(program, std::span<const ShaderDescriptor> {}, "unnamed");
    ASSERT_FALSE(descriptor);
    EXPECT_NE(descriptor.error().message.find("unnamed descriptor"), std::string::npos);
}

TEST_F(ToneMappingPassReflectionTest, RejectsMissingToneMappingDescriptor)
{
    expectShaderReflection(std::nullopt);
    EXPECT_CALL(vulkan, vkCreateDescriptorSetLayout(_, _, _, _)).Times(0);

    const auto pass = panda::core::vulkan::presentation::ToneMappingPass::create(device, 2);

    ASSERT_FALSE(pass);
    EXPECT_EQ(pass.error().code, panda::ErrorCode::InvalidArgument);
    EXPECT_NE(pass.error().message.find("incompatible descriptor interface"), std::string::npos);
}

TEST_F(ToneMappingPassReflectionTest, PropagatesShaderReflectionFailure)
{
    EXPECT_CALL(reflect, createShaderModule(_, _, _)).WillOnce(Return(SPV_REFLECT_RESULT_ERROR_ALLOC_FAILED));
    EXPECT_CALL(reflect, destroyShaderModule(_)).Times(0);
    EXPECT_CALL(vulkan, vkCreateDescriptorSetLayout(_, _, _, _)).Times(0);

    const auto pass = panda::core::vulkan::presentation::ToneMappingPass::create(device, 2);

    ASSERT_FALSE(pass);
    EXPECT_EQ(pass.error().code, panda::ErrorCode::ResourceExhausted);
}

TEST_F(ToneMappingPassReflectionTest, RejectsToneMappingDescriptorSetBeyondDeviceLimit)
{
    expectShaderReflection(1);
    EXPECT_CALL(vulkan, vkCreateDescriptorSetLayout(_, _, _, _)).Times(0);

    const auto pass = panda::core::vulkan::presentation::ToneMappingPass::create(device, 1);

    ASSERT_FALSE(pass);
    EXPECT_EQ(pass.error().code, panda::ErrorCode::Unsupported);
    EXPECT_NE(pass.error().message.find("exceeds device limits"), std::string::npos);
}

TEST_F(ToneMappingPassReflectionTest, PropagatesFailureCreatingAnEmptyDescriptorSetLayout)
{
    expectShaderReflection(1);
    EXPECT_CALL(vulkan, vkCreateDescriptorSetLayout(deviceHandle, NotNull(), nullptr, NotNull()))
        .Times(2)
        .WillOnce([this](VkDevice,
                         const VkDescriptorSetLayoutCreateInfo* info,
                         const VkAllocationCallbacks*,
                         VkDescriptorSetLayout* output) {
            EXPECT_EQ(info->bindingCount, 1U);
            *output = fakeHandle<VkDescriptorSetLayout>(nextHandle++);
            return static_cast<VkResult>(vk::Result::eSuccess);
        })
        .WillOnce([](VkDevice,
                     const VkDescriptorSetLayoutCreateInfo* info,
                     const VkAllocationCallbacks*,
                     VkDescriptorSetLayout*) {
            EXPECT_EQ(info->bindingCount, 0U);
            return static_cast<VkResult>(vk::Result::eErrorOutOfHostMemory);
        });
    EXPECT_CALL(vulkan, vkDestroyDescriptorSetLayout(deviceHandle, _, nullptr));
    EXPECT_CALL(vulkan, vkCreatePipelineLayout(_, _, _, _)).Times(0);
    EXPECT_CALL(vulkan, vkCreateSampler(_, _, _, _)).Times(0);

    const auto pass = panda::core::vulkan::presentation::ToneMappingPass::create(device, 2);

    ASSERT_FALSE(pass);
    EXPECT_EQ(pass.error().code, panda::ErrorCode::ResourceExhausted);
}

TEST_F(ToneMappingPassReflectionTest, CreatesGapLayoutsForReflectedDescriptorSet)
{
    expectShaderReflection(1);
    expectSuccessfulInitialization();

    auto pass = panda::core::vulkan::presentation::ToneMappingPass::create(device, 2);

    ASSERT_TRUE(pass);
    pass->reset();
}
