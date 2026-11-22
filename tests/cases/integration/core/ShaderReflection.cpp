#include "vulkan/renderer/ShaderReflection.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <span>
#include <test/shader/ShaderResources.hpp>

namespace
{
using panda::core::vulkan::renderer::reflectShader;
using panda::core::vulkan::renderer::ShaderDescriptorKind;
using panda::core::vulkan::renderer::ShaderStage;
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
    EXPECT_EQ(reflection->pushConstantBlocks.front().size, 16U);
    ASSERT_EQ(reflection->pushConstantBlocks.front().members.size(), 1U);
    EXPECT_EQ(reflection->pushConstantBlocks.front().members.front().offset, 0U);
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
