#include <gtest/gtest.h>

#include <algorithm>
#include <panda/Context.hpp>
#include <panda/Error.hpp>
#include <panda/Logger.hpp>
#include <panda/Texture.hpp>
#include <utility>
#include <vulkan/vulkan.hpp>

#include "LogCapture.hpp"

TEST(TextureSystem, CreatesAndReleasesSharedImageOwnersWithoutValidationErrors)
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
            .usage = vk::ImageUsageFlagBits::eSampled
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

        auto copy = *created;
        created->reset();
        ASSERT_TRUE(copy.isValid());
        auto moved = std::move(copy);
        EXPECT_TRUE(moved.isValid());
        moved.reset();
    }
    EXPECT_FALSE(std::ranges::any_of(records.entries, [](const auto& entry) {
        return entry.level == panda::log::Level::Error;
    }));
}
