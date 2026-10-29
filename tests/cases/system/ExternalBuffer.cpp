#include <gtest/gtest.h>

#include <array>
#include <cstddef>
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

    auto created = context->getExternalBufferManager().createBuffer(
        {.size = 1024, .usage = vk::BufferUsageFlagBits::eStorageBuffer});
    if (!created && created.error().code == panda::ErrorCode::Unsupported)
    {
        GTEST_SKIP() << created.error().message;
    }
    ASSERT_TRUE(created) << created.error().message;
    EXPECT_TRUE(created->isValid());
    EXPECT_TRUE(created->getBuffer().isValid());
    EXPECT_EQ(created->getSize(), 1024);
    EXPECT_GE(created->getAllocationSize(), created->getSize());
    EXPECT_TRUE(created->getHandle().isValid());
    EXPECT_EQ(created->hasSemaphore(), context->getDeviceInfo().externalSemaphoreExportEnabled);
    EXPECT_EQ(created->getSemaphoreHandle().isValid(), created->hasSemaphore());
#ifdef _WIN32
    if (created->hasSemaphore())
    {
        EXPECT_EQ(created->getSemaphoreHandle().getType(), panda::ExternalSemaphoreHandleType::OpaqueWin32);
    }
#elifdef __linux__
    if (created->hasSemaphore())
    {
        EXPECT_EQ(created->getSemaphoreHandle().getType(), panda::ExternalSemaphoreHandleType::OpaqueFd);
    }
#endif
}

TEST(ExternalBufferSystem, ReleaseSignalsTimelineAndPreventsVulkanUploadsUntilAcquire)
{
    auto context =
        panda::Context::create({.applicationName = "Panda external handoff smoke",
                                .externalBufferSupport = panda::ContextOptions::ExternalBufferSupport::Required});
    if (!context && context.error().code == panda::ErrorCode::Unsupported)
    {
        GTEST_SKIP() << context.error().message;
    }
    ASSERT_TRUE(context) << context.error().message;
    if (!context->getDeviceInfo().externalSemaphoreExportEnabled)
    {
        GTEST_SKIP() << "Selected Vulkan device cannot export a timeline semaphore";
    }

    auto& externalBuffers = context->getExternalBufferManager();
    auto buffer = externalBuffers.createBuffer(
        {.size = 1024, .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst});
    if (!buffer && buffer.error().code == panda::ErrorCode::Unsupported)
    {
        GTEST_SKIP() << buffer.error().message;
    }
    ASSERT_TRUE(buffer) << buffer.error().message;

    {
        auto batch = context->createUploadBatch();
        ASSERT_TRUE(batch);
        const auto blocked = externalBuffers.releaseToExternal(buffer->getBuffer());
        ASSERT_FALSE(blocked);
        EXPECT_EQ(blocked.error().code, panda::ErrorCode::ResourcesInUse);
    }

    const auto externalView = buffer->getBuffer();
    const auto released = externalBuffers.releaseToExternal(externalView);
    ASSERT_TRUE(released) << released.error().message;
    EXPECT_EQ(released->getValue(), 1);
    const auto duplicateRelease = externalBuffers.releaseToExternal(externalView);
    ASSERT_FALSE(duplicateRelease);
    EXPECT_EQ(duplicateRelease.error().code, panda::ErrorCode::InvalidArgument);
    const auto wrongAcquire = externalBuffers.acquireFromExternal(externalView, *released);
    ASSERT_FALSE(wrongAcquire);
    EXPECT_EQ(wrongAcquire.error().code, panda::ErrorCode::InvalidArgument);

    auto batch = context->createUploadBatch();
    ASSERT_TRUE(batch);
    const auto bytes = std::array<std::byte, 4> {};
    const auto blockedUpload = batch->write(externalView, 0, bytes);
    ASSERT_FALSE(blockedUpload);
    EXPECT_EQ(blockedUpload.error().code, panda::ErrorCode::ResourcesInUse);
}
