#include <gtest/gtest.h>

#include <panda/Context.hpp>
#include <panda/Error.hpp>
#include <vulkan/vulkan.hpp>

TEST(ExternalBufferSystem, CreatesDedicatedBufferAndOwnsPlatformHandle)
{
    auto context =
        panda::Context::create({.applicationName = "Panda external buffer smoke",
                                .externalBufferSupport = panda::ContextOptions::ExternalBufferSupport::Required});
    if (!context && context.error().code == panda::ErrorCode::Unsupported)
    {
        GTEST_SKIP() << context.error().message;
    }
    ASSERT_TRUE(context) << context.error().message;

    auto created = context->createExternalBuffer({.size = 1024, .usage = vk::BufferUsageFlagBits::eStorageBuffer});
    if (!created && created.error().code == panda::ErrorCode::Unsupported)
    {
        GTEST_SKIP() << created.error().message;
    }
    ASSERT_TRUE(created) << created.error().message;
    EXPECT_TRUE(created->isValid());
    EXPECT_TRUE(created->buffer().isValid());
    EXPECT_EQ(created->getSize(), 1024);
    EXPECT_GE(created->getAllocationSize(), created->getSize());
    EXPECT_TRUE(created->handle().isValid());
}
