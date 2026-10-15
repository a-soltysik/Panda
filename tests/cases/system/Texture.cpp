#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <panda/CompletionPoint.hpp>
#include <panda/Context.hpp>
#include <panda/Error.hpp>
#include <panda/Logger.hpp>
#include <panda/Texture.hpp>
#include <panda/UploadBatch.hpp>
#include <utility>
#include <vector>
#include <vulkan/vulkan.hpp>

#include "LogCapture.hpp"

TEST(TextureSystem, UploadsTextureRegionsAndReleasesSharedImageOwnersWithoutValidationErrors)
{
    auto records = panda::test::LogRecords {};
    const auto capture = panda::test::ProcessSinkRegistration {records};
    {
        auto context = panda::Context::create({.applicationName = "Panda texture smoke", .enableValidation = true});
        if (!context && context.error().code == panda::ErrorCode::Unsupported)
        {
            GTEST_SKIP() << context.error().message;
        }
        ASSERT_TRUE(context) << context.error().message;
        const auto description = panda::TextureDesc {
            .extent = vk::Extent2D {32, 16},
            .format = vk::Format::eR8G8B8A8Unorm,
            .usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled
        };
        auto created = context->createTexture(description);
        if (!created && created.error().code == panda::ErrorCode::Unsupported)
        {
            GTEST_SKIP() << created.error().message;
        }
        ASSERT_TRUE(created) << created.error().message;
        EXPECT_TRUE(created->isValid());
        EXPECT_EQ(created->getExtent().width, description.extent.width);
        EXPECT_EQ(created->getExtent().height, description.extent.height);
        EXPECT_EQ(created->getFormat(), description.format);
        EXPECT_EQ(created->getUsage(), description.usage);

        auto batch = context->createUploadBatch();
        ASSERT_TRUE(batch) << batch.error().message;
        const auto top = std::vector<std::byte>(std::size_t {8} * 136, std::byte {0x25});
        const auto bottom = std::vector<std::byte>(std::size_t {32} * 8 * 4, std::byte {0x6a});
        const auto topRegion = panda::TextureUploadRegion {
            .extent = vk::Extent2D {32, 8},
            .rowPitchBytes = 136
        };
        const auto bottomRegion = panda::TextureUploadRegion {
            .offset = vk::Offset2D {0,  8},
            .extent = vk::Extent2D {32, 8}
        };
        const auto topUpload = batch->write(*created, top, topRegion);
        ASSERT_TRUE(topUpload) << topUpload.error().message;
        const auto bottomUpload = batch->write(*created, bottom, bottomRegion);
        ASSERT_TRUE(bottomUpload) << bottomUpload.error().message;
        const auto submitted = batch->submit();
        ASSERT_TRUE(submitted) << submitted.error().message;
        const auto waited = context->wait(*submitted, std::chrono::seconds {10});
        ASSERT_TRUE(waited) << waited.error().message;
        ASSERT_EQ(*waited, panda::WaitStatus::Completed);

        auto copy = *created;
        created->reset();
        ASSERT_TRUE(copy.isValid());
        auto moved = std::move(copy);
        EXPECT_TRUE(moved.isValid());
        moved.reset();

        auto fullTexture = context->createTexture(description);
        ASSERT_TRUE(fullTexture) << fullTexture.error().message;
        auto fullBatch = context->createUploadBatch();
        ASSERT_TRUE(fullBatch) << fullBatch.error().message;
        const auto fullBytes = std::vector<std::byte>(std::size_t {32} * 16 * 4, std::byte {0x3c});
        const auto fullUpload = fullBatch->write(*fullTexture, fullBytes);
        ASSERT_TRUE(fullUpload) << fullUpload.error().message;
        const auto fullSubmitted = fullBatch->submit();
        ASSERT_TRUE(fullSubmitted) << fullSubmitted.error().message;
        const auto fullWaited = context->wait(*fullSubmitted, std::chrono::seconds {10});
        ASSERT_TRUE(fullWaited) << fullWaited.error().message;
        ASSERT_EQ(*fullWaited, panda::WaitStatus::Completed);
        fullTexture->reset();
    }
    const auto error = std::ranges::find_if(records.entries, [](const auto& entry) {
        return entry.level == panda::log::Level::Error;
    });
    EXPECT_EQ(error, records.entries.end()) << (error == records.entries.end() ? "" : error->message);
}
