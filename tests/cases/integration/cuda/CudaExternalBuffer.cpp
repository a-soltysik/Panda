#include <gtest/gtest.h>

#include <panda/Context.hpp>
#include <panda/Error.hpp>
#include <panda/cuda/Cuda.hpp>
#include <utility>
#include <vulkan/vulkan.hpp>

TEST(CudaExternalBufferIntegration, ImportsPandaOwnedBufferOnce)
{
    const auto availability = panda::cuda::queryAvailability();
    ASSERT_TRUE(availability.has_value()) << "CUDA Runtime status: " << availability.error().nativeCode;
    if (*availability != panda::cuda::Availability::Available)
    {
        GTEST_SKIP() << "CUDA runtime has no usable device";
    }

    auto context =
        panda::Context::create({.applicationName = "Panda CUDA external buffer integration",
                                .externalBufferSupport = panda::ContextOptions::ExternalBufferSupport::Required});
    if (!context && context.error().code == panda::ErrorCode::Unsupported)
    {
        GTEST_SKIP() << context.error().message;
    }
    ASSERT_TRUE(context) << context.error().message;

    auto session = panda::cuda::CudaSession::create(*context);
    if (!session && session.error().code == panda::ErrorCode::Unsupported)
    {
        GTEST_SKIP() << session.error().message;
    }
    ASSERT_TRUE(session) << session.error().message;

    auto externalBuffer =
        context->createExternalBuffer({.size = 1024, .usage = vk::BufferUsageFlagBits::eStorageBuffer});
    if (!externalBuffer && externalBuffer.error().code == panda::ErrorCode::Unsupported)
    {
        GTEST_SKIP() << externalBuffer.error().message;
    }
    ASSERT_TRUE(externalBuffer) << externalBuffer.error().message;

    auto imported = session->importBuffer(std::move(*externalBuffer));
    if (!imported && imported.error().code == panda::ErrorCode::Unsupported)
    {
        GTEST_SKIP() << imported.error().message;
    }
    ASSERT_TRUE(imported) << imported.error().message;
    EXPECT_TRUE(imported->isValid());
    EXPECT_EQ(imported->getSize(), 1024);
    EXPECT_TRUE(imported->buffer().isValid());
    const auto closeWhileImported = session->close();
    ASSERT_FALSE(closeWhileImported);
    EXPECT_EQ(closeWhileImported.error().code, panda::ErrorCode::ResourcesInUse);

    *imported = panda::cuda::SharedCudaBuffer {};
    ASSERT_TRUE(session->close());
}
