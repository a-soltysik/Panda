#include <gtest/gtest.h>

#include <algorithm>
#include <panda/Buffer.hpp>
#include <panda/Context.hpp>
#include <panda/Error.hpp>
#include <panda/Logger.hpp>
#include <utility>
#include <vulkan/vulkan.hpp>

#include "LogCapture.hpp"

namespace
{
void exerciseOwnership(panda::Context& context, panda::BufferMemory intent)
{
    auto created =
        context.createBuffer({.size = 257, .usage = vk::BufferUsageFlagBits::eStorageBuffer, .memory = intent});
    ASSERT_TRUE(created) << created.error().message;
    auto second =
        context.createBuffer({.size = 257, .usage = vk::BufferUsageFlagBits::eStorageBuffer, .memory = intent});
    ASSERT_TRUE(second) << second.error().message;
    auto copy = *created;
    EXPECT_EQ(copy.getSize(), 257);
    EXPECT_EQ(second->getSize(), 257);
    created->reset();
    ASSERT_TRUE(copy.isValid());
    auto moved = std::move(context);
    // isValid explicitly supports moved-from contexts.
    // NOLINTNEXTLINE(bugprone-use-after-move,hicpp-invalid-access-moved)
    EXPECT_FALSE(context.isValid());
    EXPECT_EQ(copy.getSize(), 257);
    copy.reset();
    second->reset();
}

class BufferSystem : public testing::TestWithParam<panda::BufferMemory>
{
};
}

TEST_P(BufferSystem, CreatesAndReleasesPublicOwnersWithoutValidationErrors)
{
    auto records = panda::test::LogRecords {};
    const auto capture = panda::test::ProcessSinkRegistration {records};
    {
        auto context = panda::Context::create({.applicationName = "Panda buffer smoke", .enableValidation = true});
        if (!context && context.error().code == panda::ErrorCode::Unsupported)
        {
            GTEST_SKIP() << context.error().message;
        }
        ASSERT_TRUE(context) << context.error().message;
        exerciseOwnership(*context, GetParam());
    }
    EXPECT_FALSE(std::ranges::any_of(records.entries, [](const auto& entry) {
        return entry.level == panda::log::Level::Error;
    }));
}

INSTANTIATE_TEST_SUITE_P(MemoryIntents,
                         BufferSystem,
                         testing::Values(panda::BufferMemory::Device,
                                         panda::BufferMemory::Upload,
                                         panda::BufferMemory::Readback));
