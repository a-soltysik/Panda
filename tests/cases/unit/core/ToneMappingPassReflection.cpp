// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan.hpp>
// clang-format on

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <include/spirv/unified1/spirv.h>
#include <spirv_reflect.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <panda/base/Error.hpp>
#include <span>

#include "FakeHandle.hpp"
#include "ScopedMock.hpp"
#include "VulkanTestSupport.hpp"
#include "external/spirv_reflect/SpirvReflectMock.hpp"
#include "presentation/ToneMappingPass.hpp"

namespace
{
using panda::test::fakeHandle;
using panda::test::ScopedMock;
using panda::test::SpirvReflectMock;
using testing::_;
using testing::AnyNumber;
using testing::IsNull;
using testing::NotNull;
using testing::Return;
using testing::StrEq;

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

auto makeSampledImageType() -> SpvReflectTypeDescription
{
    auto type = SpvReflectTypeDescription {};
    type.type_flags = SPV_REFLECT_TYPE_FLAG_FLOAT;
    type.traits.numeric.scalar.width = 32;
    return type;
}

class ToneMappingPassReflectionTest : public panda::test::VulkanTestFixture
{
protected:
    void expectShaderReflection(std::optional<std::uint32_t> descriptorSet)
    {
        descriptor = SpvReflectDescriptorBinding {};
        descriptor.name = "hdrColor";
        descriptor.set = descriptorSet.value_or(0U);
        descriptor.binding = 0;
        descriptor.count = 1;
        descriptor.descriptor_type = SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptor.image.dim = SpvDim2D;
        descriptor.type_description = &imageType;
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
    std::uintptr_t nextHandle {10};
    SpvReflectEntryPoint entryPoint {makeEntryPoint(SPV_REFLECT_SHADER_STAGE_VERTEX_BIT)};
    SpvReflectDescriptorBinding descriptor {};
    SpvReflectTypeDescription imageType {makeSampledImageType()};
    SpvReflectSpecializationConstant specialization {};
    std::size_t moduleIndex {};
};
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
}
