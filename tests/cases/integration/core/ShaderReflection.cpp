#include "vulkan/renderer/ShaderReflection.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <span>
#include <test/shader/ShaderResources.hpp>
#include <vector>

namespace
{
using panda::core::vulkan::renderer::buildShaderProgramLayout;
using panda::core::vulkan::renderer::reflectGraphicsProgram;
using panda::core::vulkan::renderer::reflectShader;
using panda::core::vulkan::renderer::ShaderDescriptorKind;
using panda::core::vulkan::renderer::ShaderInterfaceFormat;
using panda::core::vulkan::renderer::ShaderStage;
using panda::core::vulkan::renderer::specializationConstantId;
}

TEST(ShaderReflection, ReportsDescriptorAndHostLayout)
{
    const auto shader = test::shader::findShader("fixtures/shaders/basic.vert");
    ASSERT_TRUE(shader.has_value());

    const auto reflection = reflectShader(shader->words, ShaderStage::Vertex, shader->key);
    ASSERT_TRUE(reflection) << reflection.error().message;
    ASSERT_EQ(reflection->descriptors.size(), 1U);
    const auto& descriptor = reflection->descriptors.front();
    EXPECT_EQ(descriptor.set, 2U);
    EXPECT_EQ(descriptor.binding, 3U);
    EXPECT_EQ(descriptor.kind, ShaderDescriptorKind::UniformBuffer);
    EXPECT_EQ(descriptor.blockSize, 80U);
    ASSERT_EQ(descriptor.members.size(), 2U);
    EXPECT_EQ(descriptor.members.front().name, "tint");
    EXPECT_EQ(descriptor.members.front().offset, 0U);
    EXPECT_EQ(descriptor.members.front().size, 16U);
    EXPECT_EQ(descriptor.members.back().name, "transform");
    EXPECT_EQ(descriptor.members.back().offset, 16U);
    EXPECT_EQ(descriptor.members.back().size, 64U);

    ASSERT_EQ(reflection->pushConstantBlocks.size(), 1U);
    EXPECT_EQ(reflection->pushConstantBlocks.front().offset, 16U);
    EXPECT_EQ(reflection->pushConstantBlocks.front().size, 16U);
    ASSERT_EQ(reflection->pushConstantBlocks.front().members.size(), 1U);
    EXPECT_EQ(reflection->pushConstantBlocks.front().members.front().offset, 16U);

    ASSERT_EQ(reflection->inputs.size(), 1U);
    EXPECT_EQ(reflection->inputs.front().name, "testPosition");
    EXPECT_EQ(reflection->inputs.front().location, 6U);
    EXPECT_EQ(reflection->inputs.front().format, ShaderInterfaceFormat::Float4);
    ASSERT_EQ(reflection->outputs.size(), 1U);
    EXPECT_EQ(reflection->outputs.front().location, 2U);

    const auto fragmentShader = test::shader::findShader("fixtures/shaders/basic.frag");
    ASSERT_TRUE(fragmentShader.has_value());
    const auto fragmentReflection = reflectShader(fragmentShader->words, ShaderStage::Fragment, fragmentShader->key);
    ASSERT_TRUE(fragmentReflection) << fragmentReflection.error().message;
    const auto shaderStages = std::array {*reflection, *fragmentReflection};
    const auto programLayout = buildShaderProgramLayout(shaderStages, "basic test program");
    ASSERT_TRUE(programLayout) << programLayout.error().message;
    ASSERT_EQ(programLayout->descriptors.size(), 1U);
    EXPECT_EQ(programLayout->descriptors.front().set, 2U);
    EXPECT_EQ(programLayout->descriptors.front().binding, 3U);
    EXPECT_EQ(programLayout->descriptors.front().stageLayouts.size(), 2U);
    ASSERT_EQ(programLayout->vertexInputs.size(), 1U);
    EXPECT_EQ(programLayout->vertexInputs.front().location, 6U);
    ASSERT_EQ(programLayout->specializationConstants.size(), 1U);
    EXPECT_EQ(programLayout->specializationConstants.front().name, "testMode");
    EXPECT_EQ(programLayout->specializationConstants.front().stage, ShaderStage::Fragment);
    EXPECT_EQ(specializationConstantId(*programLayout, ShaderStage::Fragment, "testMode", "basic test program"), 11U);
}

TEST(ShaderReflection, RejectsStageMismatchAndInvalidHeader)
{
    const auto shader = test::shader::findShader("fixtures/shaders/basic.vert");
    ASSERT_TRUE(shader.has_value());
    EXPECT_FALSE(reflectShader(shader->words, ShaderStage::Fragment, shader->key));

    static constexpr auto invalidHeader = std::array {0U, 0U, 0U, 0U, 0U};
    EXPECT_FALSE(reflectShader(invalidHeader, ShaderStage::Vertex, "invalid"));
    EXPECT_FALSE(reflectShader(std::span<const std::uint32_t> {}, ShaderStage::Vertex, "empty"));
}

TEST(ShaderReflection, RejectsSingleElementDescriptorArrays)
{
    const auto vertexShader = test::shader::findShader("fixtures/shaders/basic.vert");
    const auto fragmentShader = test::shader::findShader("fixtures/shaders/descriptor_array.frag");
    ASSERT_TRUE(vertexShader.has_value());
    ASSERT_TRUE(fragmentShader.has_value());

    const auto reflection = reflectShader(fragmentShader->words, ShaderStage::Fragment, fragmentShader->key);
    ASSERT_TRUE(reflection) << reflection.error().message;
    ASSERT_EQ(reflection->descriptors.size(), 1U);
    EXPECT_EQ(reflection->descriptors.front().arrayDimensions, (std::vector<std::uint32_t> {1}));

    const auto program = reflectGraphicsProgram(vertexShader->words, fragmentShader->words, "array test");
    ASSERT_FALSE(program);
    EXPECT_NE(program.error().message.find("unsupported descriptor"), std::string::npos);
}

TEST(ShaderReflection, RejectsNonzeroInterfaceComponents)
{
    const auto shader = test::shader::findShader("fixtures/shaders/component.frag");
    ASSERT_TRUE(shader.has_value());

    const auto reflection = reflectShader(shader->words, ShaderStage::Fragment, shader->key);

    ASSERT_FALSE(reflection);
    EXPECT_NE(reflection.error().message.find("component"), std::string::npos);
}
