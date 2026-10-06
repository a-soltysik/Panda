#include <gtest/gtest.h>

#include <panda/Context.hpp>
#include <panda/Error.hpp>
#include <string>
#include <thread>
#include <utility>
#include <vulkan/vulkan.hpp>

TEST(VulkanContextSystem, SelectsAndOwnsRequiredDevice)
{
    auto created = panda::Context::create({.applicationName = "Panda Vulkan context smoke", .enableValidation = true});
    if (!created && created.error().code == panda::ErrorCode::Unsupported)
    {
        GTEST_SKIP() << created.error().message;
    }
    ASSERT_TRUE(created.has_value()) << created.error().message;
    EXPECT_TRUE(created->isValid());
    const auto info = created->getDeviceInfo();
    EXPECT_FALSE(info.name.empty());
    EXPECT_GE(info.apiVersion, vk::ApiVersion13);
    auto moved = std::move(*created);
    EXPECT_FALSE(created->isValid());
    EXPECT_TRUE(moved.isValid());
    EXPECT_EQ(moved.getDeviceInfo().name, info.name);
    auto nameFromWorker = std::string {};
    auto worker = std::jthread {[owned = std::move(moved), &nameFromWorker] {
        nameFromWorker = owned.getDeviceInfo().name;
    }};
    worker.join();
    EXPECT_EQ(nameFromWorker, info.name);
}
