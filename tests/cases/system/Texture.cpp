#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <panda/Engine.hpp>
#include <panda/base/Error.hpp>
#include <panda/diagnostics/Logger.hpp>
#include <panda/resources/PixelFormat.hpp>
#include <panda/resources/Texture.hpp>
#include <panda/submission/CompletionPoint.hpp>
#include <panda/submission/UploadBatch.hpp>
#include <span>
#include <utility>
#include <vector>

#include "LogCapture.hpp"

TEST(TextureSystem, UploadsTextureRegionsAndReleasesSharedImageOwnersWithoutValidationErrors)
{
    auto records = panda::test::LogRecords {};
    const auto capture = panda::test::ProcessSinkRegistration {records};
    {
        auto context = panda::Engine::create({.applicationName = "Panda texture smoke", .enableValidation = true});
        if (!context && context.error().code == panda::ErrorCode::Unsupported)
        {
            GTEST_SKIP() << context.error().message;
        }
        ASSERT_TRUE(context) << context.error().message;
        const auto description = panda::TextureDesc {
            .extent = glm::uvec2 {32, 16},
            .format = ::panda::PixelFormat::Rgba8Unorm,
            .usage = ::panda::TextureUsage::TransferDestination | ::panda::TextureUsage::Sampled
        };
        auto created = context->getResourceManager().createTexture(description);
        if (!created && created.error().code == panda::ErrorCode::Unsupported)
        {
            GTEST_SKIP() << created.error().message;
        }
        ASSERT_TRUE(created) << created.error().message;
        EXPECT_TRUE(created->isValid());
        EXPECT_EQ(created->getExtent().x, description.extent.x);
        EXPECT_EQ(created->getExtent().y, description.extent.y);
        EXPECT_EQ(created->getFormat(), description.format);
        EXPECT_EQ(created->getUsage(), description.usage);

        auto batch = context->getSubmissionManager().createUploadBatch();
        ASSERT_TRUE(batch) << batch.error().message;
        const auto top = std::vector<std::byte>(std::size_t {8} * 136, std::byte {0x25});
        const auto bottom = std::vector<std::byte>(std::size_t {32} * 8 * 4, std::byte {0x6a});
        const auto topRegion = panda::TextureUploadRegion {
            .extent = glm::uvec2 {32, 8},
            .rowPitchBytes = 136
        };
        const auto bottomRegion = panda::TextureUploadRegion {
            .offset = glm::ivec2 {0,  8},
            .extent = glm::uvec2 {32, 8}
        };
        const auto topUpload = batch->write(*created, top, topRegion);
        ASSERT_TRUE(topUpload) << topUpload.error().message;
        const auto bottomUpload = batch->write(*created, bottom, bottomRegion);
        ASSERT_TRUE(bottomUpload) << bottomUpload.error().message;
        const auto submitted = batch->submit();
        ASSERT_TRUE(submitted) << submitted.error().message;
        const auto waited = context->getSubmissionManager().wait(*submitted, std::chrono::seconds {10});
        ASSERT_TRUE(waited) << waited.error().message;
        ASSERT_EQ(*waited, panda::WaitStatus::Completed);

        auto copy = *created;
        created->reset();
        ASSERT_TRUE(copy.isValid());
        auto moved = std::move(copy);
        EXPECT_TRUE(moved.isValid());
        moved.reset();

        auto fullTexture = context->getResourceManager().createTexture(description);
        ASSERT_TRUE(fullTexture) << fullTexture.error().message;
        auto fullBatch = context->getSubmissionManager().createUploadBatch();
        ASSERT_TRUE(fullBatch) << fullBatch.error().message;
        const auto fullBytes = std::vector<std::byte>(std::size_t {32} * 16 * 4, std::byte {0x3c});
        const auto fullUpload = fullBatch->write(*fullTexture, fullBytes);
        ASSERT_TRUE(fullUpload) << fullUpload.error().message;
        const auto fullSubmitted = fullBatch->submit();
        ASSERT_TRUE(fullSubmitted) << fullSubmitted.error().message;
        const auto fullWaited = context->getSubmissionManager().wait(*fullSubmitted, std::chrono::seconds {10});
        ASSERT_TRUE(fullWaited) << fullWaited.error().message;
        ASSERT_EQ(*fullWaited, panda::WaitStatus::Completed);
        fullTexture->reset();
    }
    const auto error = std::ranges::find_if(records.entries, [](const auto& entry) {
        return entry.level == panda::log::Level::Error;
    });
    EXPECT_EQ(error, records.entries.end()) << (error == records.entries.end() ? "" : error->message);
}

TEST(TextureSystem, UploadsCompleteSuppliedAndGeneratedMipChains)
{
    auto context = panda::Engine::create({.applicationName = "Panda mip chain upload", .enableValidation = true});
    if (!context && context.error().code == panda::ErrorCode::Unsupported)
    {
        GTEST_SKIP() << context.error().message;
    }
    ASSERT_TRUE(context) << context.error().message;

    const auto unsupportedGeneratedFormat = context->getResourceManager().createTexture({
        .extent = {4, 4},
        .format = panda::PixelFormat::Rgba16Float,
        .usage = panda::TextureUsage::TransferDestination | panda::TextureUsage::Sampled,
        .mipPolicy = panda::TextureMipPolicy::Generate
    });
    ASSERT_FALSE(unsupportedGeneratedFormat);
    EXPECT_EQ(unsupportedGeneratedFormat.error().code, panda::ErrorCode::InvalidArgument);

    auto supplied = context->getResourceManager().createTexture({
        .extent = {4, 4},
        .format = panda::PixelFormat::Rgba8Srgb,
        .usage = panda::TextureUsage::TransferDestination | panda::TextureUsage::Sampled,
        .mipPolicy = panda::TextureMipPolicy::Supplied
    });
    ASSERT_TRUE(supplied) << supplied.error().message;
    ASSERT_EQ(supplied->getMipLevels(), 3U);
    auto suppliedBatch = context->getSubmissionManager().createUploadBatch();
    ASSERT_TRUE(suppliedBatch) << suppliedBatch.error().message;
    const auto base = std::vector<std::byte>(std::size_t {4} * 4U * 4U, std::byte {0x11});
    const auto middle = std::vector<std::byte>(std::size_t {2} * 2U * 4U, std::byte {0x22});
    const auto smallest = std::vector<std::byte>(4U, std::byte {0x33});
    ASSERT_TRUE(suppliedBatch->write(*supplied, base));
    const auto incomplete = suppliedBatch->submit();
    ASSERT_FALSE(incomplete);
    EXPECT_EQ(incomplete.error().code, panda::ErrorCode::InvalidArgument);
    ASSERT_TRUE(suppliedBatch->write(*supplied,
                                     middle,
                                     {
                                         .extent = {2, 2},
                                         .mipLevel = 1
    }));
    ASSERT_TRUE(suppliedBatch->write(*supplied,
                                     smallest,
                                     {
                                         .extent = {1, 1},
                                         .mipLevel = 2
    }));
    const auto suppliedCompletion = suppliedBatch->submit();
    ASSERT_TRUE(suppliedCompletion) << suppliedCompletion.error().message;
    const auto suppliedWait = context->getSubmissionManager().wait(*suppliedCompletion, std::chrono::seconds {10});
    ASSERT_TRUE(suppliedWait) << suppliedWait.error().message;
    ASSERT_EQ(*suppliedWait, panda::WaitStatus::Completed);

    auto generated = context->getResourceManager().createTexture({
        .extent = {2, 2},
        .format = panda::PixelFormat::Rgba8Srgb,
        .usage = panda::TextureUsage::TransferDestination | panda::TextureUsage::Sampled,
        .mipPolicy = panda::TextureMipPolicy::Generate
    });
    ASSERT_TRUE(generated) << generated.error().message;
    ASSERT_EQ(generated->getMipLevels(), 2U);
    auto generatedBatch = context->getSubmissionManager().createUploadBatch();
    ASSERT_TRUE(generatedBatch) << generatedBatch.error().message;
    const auto generatedBase = std::vector<std::byte> {std::byte {0},
                                                       std::byte {0},
                                                       std::byte {0},
                                                       std::byte {255},
                                                       std::byte {255},
                                                       std::byte {255},
                                                       std::byte {255},
                                                       std::byte {255},
                                                       std::byte {255},
                                                       std::byte {255},
                                                       std::byte {255},
                                                       std::byte {255},
                                                       std::byte {0},
                                                       std::byte {0},
                                                       std::byte {0},
                                                       std::byte {255}};
    ASSERT_TRUE(generatedBatch->write(*generated, generatedBase));
    const auto generatedCompletion = generatedBatch->submit();
    ASSERT_TRUE(generatedCompletion) << generatedCompletion.error().message;
    const auto generatedWait = context->getSubmissionManager().wait(*generatedCompletion, std::chrono::seconds {10});
    ASSERT_TRUE(generatedWait) << generatedWait.error().message;
    EXPECT_EQ(*generatedWait, panda::WaitStatus::Completed);
}

TEST(TextureSystem, UploadsEveryCubemapFaceAndSuppliedMipBeforeSubmission)
{
    auto context = panda::Engine::create({.applicationName = "Panda cubemap upload", .enableValidation = true});
    if (!context && context.error().code == panda::ErrorCode::Unsupported)
    {
        GTEST_SKIP() << context.error().message;
    }
    ASSERT_TRUE(context) << context.error().message;

    auto cubemap = context->getResourceManager().createTexture({
        .extent = {4, 4},
        .format = panda::PixelFormat::Rgba16Float,
        .usage = panda::TextureUsage::TransferDestination | panda::TextureUsage::Sampled,
        .mipPolicy = panda::TextureMipPolicy::Supplied,
        .type = panda::TextureType::CubeMap
    });
    if (!cubemap && cubemap.error().code == panda::ErrorCode::Unsupported)
    {
        GTEST_SKIP() << cubemap.error().message;
    }
    ASSERT_TRUE(cubemap) << cubemap.error().message;
    ASSERT_EQ(cubemap->getType(), panda::TextureType::CubeMap);
    ASSERT_EQ(cubemap->getMipLevels(), 3U);

    const auto levels =
        std::array {std::vector<std::byte>(128U), std::vector<std::byte>(32U), std::vector<std::byte>(8U)};
    auto batch = context->getSubmissionManager().createUploadBatch();
    ASSERT_TRUE(batch) << batch.error().message;
    for (auto mipLevel = std::uint32_t {}; mipLevel < levels.size(); ++mipLevel)
    {
        const auto extent = 4U >> mipLevel;
        const auto region = panda::TextureUploadRegion {
            .extent = {extent, extent},
            .mipLevel = mipLevel,
            .arrayLayer = 0
        };
        ASSERT_TRUE(batch->write(*cubemap, levels.at(mipLevel), region));
        if (mipLevel == 0)
        {
            EXPECT_FALSE(batch->write(*cubemap, levels.at(mipLevel), region));
        }
    }
    const auto incomplete = batch->submit();
    ASSERT_FALSE(incomplete);
    EXPECT_EQ(incomplete.error().code, panda::ErrorCode::InvalidArgument);

    for (auto arrayLayer = std::uint32_t {1}; arrayLayer < 6; ++arrayLayer)
    {
        for (auto mipLevel = std::uint32_t {}; mipLevel < levels.size(); ++mipLevel)
        {
            const auto extent = 4U >> mipLevel;
            ASSERT_TRUE(batch->write(*cubemap,
                                     levels.at(mipLevel),
                                     {
                                         .extent = {extent, extent},
                                         .mipLevel = mipLevel,
                                         .arrayLayer = arrayLayer
            }));
        }
    }
    const auto completion = batch->submit();
    ASSERT_TRUE(completion) << completion.error().message;
    const auto waited = context->getSubmissionManager().wait(*completion, std::chrono::seconds {10});
    ASSERT_TRUE(waited) << waited.error().message;
    EXPECT_EQ(*waited, panda::WaitStatus::Completed);
}

TEST(TextureSystem, GeneratesAndUploadsPrefilteredCubemapMipChain)
{
    auto context = panda::Engine::create({.applicationName = "Panda generated cubemap", .enableValidation = true});
    if (!context && context.error().code == panda::ErrorCode::Unsupported)
    {
        GTEST_SKIP() << context.error().message;
    }
    ASSERT_TRUE(context) << context.error().message;

    auto invalidSamples = context->getResourceManager().createTexture({
        .extent = {2, 2},
        .format = panda::PixelFormat::Rgba16Float,
        .usage = panda::TextureUsage::TransferDestination | panda::TextureUsage::Sampled,
        .mipPolicy = panda::TextureMipPolicy::GenerateSpecularEnvironment,
        .specularEnvironmentSamplesPerTexel = 0,
        .type = panda::TextureType::CubeMap
    });
    ASSERT_FALSE(invalidSamples);
    EXPECT_EQ(invalidSamples.error().code, panda::ErrorCode::InvalidArgument);

    auto cubemap = context->getResourceManager().createTexture({
        .extent = {2, 2},
        .format = panda::PixelFormat::Rgba16Float,
        .usage = panda::TextureUsage::TransferDestination | panda::TextureUsage::Sampled,
        .mipPolicy = panda::TextureMipPolicy::GenerateSpecularEnvironment,
        .specularEnvironmentSamplesPerTexel = 4,
        .type = panda::TextureType::CubeMap
    });
    if (!cubemap && cubemap.error().code == panda::ErrorCode::Unsupported)
    {
        GTEST_SKIP() << cubemap.error().message;
    }
    ASSERT_TRUE(cubemap) << cubemap.error().message;
    ASSERT_EQ(cubemap->getMipLevels(), 2U);

    auto sourceFaces = std::array<std::vector<std::uint16_t>, 6> {};
    auto batch = context->getSubmissionManager().createUploadBatch();
    ASSERT_TRUE(batch) << batch.error().message;
    for (auto face = std::uint32_t {}; face < 6U; ++face)
    {
        sourceFaces.at(face).assign(std::size_t {2U} * 2U * 4U, 0x3c00);
        ASSERT_TRUE(batch->write(*cubemap,
                                 std::as_bytes(std::span {
                                     sourceFaces.at(face)
        }),
                                 {.extent = {2, 2}, .arrayLayer = face}));
        if (face == 0)
        {
            const auto duplicate = batch->write(*cubemap,
                                                std::as_bytes(std::span {
                                                    sourceFaces.at(face)
            }),
                                                {.extent = {2, 2}, .arrayLayer = face});
            ASSERT_FALSE(duplicate);
            EXPECT_EQ(duplicate.error().code, panda::ErrorCode::InvalidArgument);
        }
        if (face == 4)
        {
            const auto incomplete = batch->submit();
            ASSERT_FALSE(incomplete);
            EXPECT_EQ(incomplete.error().code, panda::ErrorCode::InvalidArgument);
        }
    }
    const auto completion = batch->submit();
    ASSERT_TRUE(completion) << completion.error().message;
    const auto waited = context->getSubmissionManager().wait(*completion, std::chrono::seconds {10});
    ASSERT_TRUE(waited) << waited.error().message;
    EXPECT_EQ(*waited, panda::WaitStatus::Completed);
}
