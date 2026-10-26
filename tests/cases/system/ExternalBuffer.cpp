#include <gtest/gtest.h>

#include <panda/Context.hpp>
#include <panda/Error.hpp>
#include <panda/ExternalBuffer.hpp>
#include <panda/ExternalSemaphoreHandle.hpp>
#include <vulkan/vulkan.hpp>

TEST(ExternalBufferSystem, DefaultConstructsAnEmptyOwner)
{
    const auto empty = panda::ExternalBuffer {};
    EXPECT_FALSE(empty.isValid());
    EXPECT_FALSE(empty.hasSemaphore());
}

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
    EXPECT_EQ(created->hasSemaphore(), context->getDeviceInfo().externalSemaphoreExportEnabled);
    EXPECT_EQ(created->semaphoreHandle().isValid(), created->hasSemaphore());
#ifdef _WIN32
    if (created->hasSemaphore())
    {
        EXPECT_EQ(created->semaphoreHandle().getType(), panda::ExternalSemaphoreHandleType::OpaqueWin32);
    }
#elifdef __linux__
    if (created->hasSemaphore())
    {
        EXPECT_EQ(created->semaphoreHandle().getType(), panda::ExternalSemaphoreHandleType::OpaqueFd);
    }
#endif
}
