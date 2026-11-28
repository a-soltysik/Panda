#include <gtest/gtest.h>

#include <array>
#include <panda/base/Error.hpp>
#include <panda/rendering/ShaderProgram.hpp>
#include <span>
#include <string>
#include <vector>

#include "vulkan/renderer/ShaderReflection.hpp"

namespace
{
using panda::core::vulkan::renderer::buildShaderProgramLayout;
using panda::core::vulkan::renderer::ShaderBlockMember;
using panda::core::vulkan::renderer::ShaderDescriptor;
using panda::core::vulkan::renderer::ShaderDescriptorKind;
using panda::core::vulkan::renderer::ShaderInterfaceFormat;
using panda::core::vulkan::renderer::ShaderProgramLayout;
using panda::core::vulkan::renderer::ShaderReflection;
using panda::core::vulkan::renderer::ShaderStage;
using panda::core::vulkan::renderer::specializationConstantId;
using panda::core::vulkan::renderer::validateShaderBlockMembers;
using panda::core::vulkan::renderer::validateShaderProgramDescriptors;
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
                                           .members = {member},
                                           .sampledImage = {}}},
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
                                           .members = {member},
                                           .sampledImage = {}}},
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
                                                           .members = {},
                                                           .sampledImage = {}}},
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
                                                             .members = {},
                                                             .sampledImage = {}}},
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
                          .members = {},
                          .sampledImage = {}}
    };
    auto program = ShaderProgramLayout {};
    program.descriptors.push_back({.set = 1,
                                   .binding = 1,
                                   .count = 1,
                                   .kind = ShaderDescriptorKind::CombinedImageSampler,
                                   .stageLayouts = {},
                                   .names = {"baseColorMap"},
                                   .sampledImage = {}});

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
                                                                .members = {},
                                                                .sampledImage = {}}},
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
                                   .names = {},
                                   .sampledImage = {}});
    const auto descriptor = validateShaderProgramDescriptors(program, std::span<const ShaderDescriptor> {}, "unnamed");
    ASSERT_FALSE(descriptor);
    EXPECT_NE(descriptor.error().message.find("unnamed descriptor"), std::string::npos);
}
