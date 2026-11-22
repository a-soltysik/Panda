#include <gtest/gtest.h>

#include <test/shader/ShaderResources.hpp>

TEST(ShaderResources, EmbedsValidatedShadersByRelativePath)
{
    const auto vertex = test::shader::findShader("fixtures/shaders/basic.vert");
    ASSERT_TRUE(vertex.has_value());
    EXPECT_EQ(vertex->stage, test::shader::Stage::Vertex);
    ASSERT_FALSE(vertex->words.empty());
    EXPECT_EQ(vertex->words.front(), 0x07230203U);

    const auto fragment = test::shader::findShader("fixtures/shaders/basic.frag");
    ASSERT_TRUE(fragment.has_value());
    EXPECT_EQ(fragment->stage, test::shader::Stage::Fragment);
    ASSERT_FALSE(fragment->words.empty());
    EXPECT_EQ(fragment->words.front(), 0x07230203U);
    EXPECT_FALSE(test::shader::findShader("basic.vert").has_value());

    const auto left = test::shader::findShader("fixtures/shaders/left/marker.vert");
    const auto right = test::shader::findShader("fixtures/shaders/right/marker.vert");
    ASSERT_TRUE(left.has_value());
    ASSERT_TRUE(right.has_value());
    EXPECT_NE(left->key, right->key);
}
