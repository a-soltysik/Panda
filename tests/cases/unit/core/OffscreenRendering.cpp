// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <vulkan/vulkan_core.h>

#include <expected>
#include <memory>
#include <optional>
#include <panda/base/Error.hpp>
#include <tuple>
#include <utility>

#include "FakeHandle.hpp"
#include "ScopedMock.hpp"
#include "external/vulkan/VulkanMock.hpp"
#include "panda/core/SubmissionQueueMock.hpp"
#include "vulkan/SubmissionQueue.hpp"
#include "vulkan/presentation/OffscreenRenderBackend.hpp"
#include "vulkan/resources/memory/MemoryPool.hpp"

namespace
{
using panda::test::fakeHandle;
using testing::_;
using testing::NotNull;

constexpr auto success = static_cast<VkResult>(vk::Result::eSuccess);
constexpr auto initializationFailure = static_cast<VkResult>(vk::Result::eErrorInitializationFailed);

class OffscreenRenderBackendTest : public testing::Test
{
protected:
    void SetUp() override
    {
        EXPECT_CALL(submissionQueue, construct(_, 0));
        _submissions.emplace(_device, 0);
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceMemoryProperties(fakeHandle<VkPhysicalDevice>(2), NotNull()))
            .WillOnce([](VkPhysicalDevice, VkPhysicalDeviceMemoryProperties* output) {
                *output = VkPhysicalDeviceMemoryProperties {};
            });
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceProperties2(fakeHandle<VkPhysicalDevice>(2), NotNull()))
            .WillOnce([](VkPhysicalDevice, VkPhysicalDeviceProperties2* output) {
                auto* const maintenance = static_cast<VkPhysicalDeviceMaintenance3Properties*>(output->pNext);
                ASSERT_NE(maintenance, nullptr);
                maintenance->maxMemoryAllocationSize = 1024;
            });
        _pool = panda::core::vulkan::resources::MemoryPool::create(_physicalDevice, _device);
        _rendering = std::make_unique<panda::core::vulkan::presentation::OffscreenRenderBackend>(
            panda::core::vulkan::presentation::OffscreenRenderBackendCreateInfo {
                .physicalDevice = vk::PhysicalDevice {fakeHandle<VkPhysicalDevice>(2)},
                .device = _device,
                .queueFamily = 0,
                .submissions = *_submissions,
                .pool = *_pool,
                .performance = nullptr,
                .textureFilteringPreset = nullptr,
                .maxLights = 0});
    }

    void TearDown() override
    {
        _rendering.reset();
        _pool.reset();
        _submissions.reset();
        std::ignore = _device.release();
        std::ignore = _instance.release();
    }

    auto rendering() -> panda::core::vulkan::presentation::OffscreenRenderBackend&
    {
        return *_rendering;
    }

    panda::test::ScopedMock<panda::test::VulkanMock> vulkan;
    panda::test::ScopedMock<panda::test::SubmissionQueueMock> submissionQueue;

private:
    vk::raii::Context _loader {vkGetInstanceProcAddr};
    vk::raii::Instance _instance {_loader, fakeHandle<VkInstance>(1)};
    vk::raii::PhysicalDevice _physicalDevice {_instance, fakeHandle<VkPhysicalDevice>(2)};
    vk::raii::Device _device {_physicalDevice, fakeHandle<VkDevice>(3)};
    std::optional<panda::detail::SubmissionQueue> _submissions;
    std::unique_ptr<panda::core::vulkan::resources::MemoryPool> _pool;
    std::unique_ptr<panda::core::vulkan::presentation::OffscreenRenderBackend> _rendering;
};
}

TEST_F(OffscreenRenderBackendTest, InitializationFailureIsRetryableAndSuccessIsIdempotent)
{
    auto* const firstPool = fakeHandle<VkCommandPool>(10);
    auto* const secondPool = fakeHandle<VkCommandPool>(11);
    auto* const firstCommand = fakeHandle<VkCommandBuffer>(12);
    auto* const secondCommand = fakeHandle<VkCommandBuffer>(13);
    {
        const auto sequence = testing::InSequence {};
        EXPECT_CALL(vulkan, vkCreateCommandPool(fakeHandle<VkDevice>(3), NotNull(), nullptr, NotNull()))
            .WillOnce([](VkDevice, const VkCommandPoolCreateInfo*, const VkAllocationCallbacks*, VkCommandPool*) {
                return initializationFailure;
            });
        EXPECT_CALL(vulkan, vkCreateCommandPool(fakeHandle<VkDevice>(3), NotNull(), nullptr, NotNull()))
            .WillOnce([firstPool](VkDevice,
                                  const VkCommandPoolCreateInfo*,
                                  const VkAllocationCallbacks*,
                                  VkCommandPool* out) {
                *out = firstPool;
                return success;
            });
        EXPECT_CALL(vulkan, vkAllocateCommandBuffers(fakeHandle<VkDevice>(3), NotNull(), NotNull()))
            .WillOnce([firstCommand](VkDevice, const VkCommandBufferAllocateInfo*, VkCommandBuffer* out) {
                *out = firstCommand;
                return success;
            });
        EXPECT_CALL(vulkan, vkCreateCommandPool(fakeHandle<VkDevice>(3), NotNull(), nullptr, NotNull()))
            .WillOnce([secondPool](VkDevice,
                                   const VkCommandPoolCreateInfo*,
                                   const VkAllocationCallbacks*,
                                   VkCommandPool* out) {
                *out = secondPool;
                return success;
            });
        EXPECT_CALL(vulkan, vkAllocateCommandBuffers(fakeHandle<VkDevice>(3), NotNull(), NotNull()))
            .WillOnce([secondCommand](VkDevice, const VkCommandBufferAllocateInfo*, VkCommandBuffer* out) {
                *out = secondCommand;
                return success;
            });
    }
    EXPECT_CALL(vulkan, vkDestroyCommandPool(fakeHandle<VkDevice>(3), firstPool, nullptr));
    EXPECT_CALL(vulkan, vkDestroyCommandPool(fakeHandle<VkDevice>(3), secondPool, nullptr));

    const auto failed = rendering().initialize();
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code, panda::ErrorCode::BackendFailure);
    ASSERT_TRUE(rendering().initialize());
    EXPECT_TRUE(rendering().initialize());
}

TEST_F(OffscreenRenderBackendTest, UninitializedShellDrainsWithoutNativeWork)
{
    EXPECT_TRUE(rendering().drain());
}
