#pragma once

// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <optional>
#include <panda/Context.hpp>
#include <panda/Error.hpp>
#include <panda/WindowSurface.hpp>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

#include "ScopedMock.hpp"
#include "VulkanTestSupport.hpp"
#include "external/vulkan/VulkanMock.hpp"
#include "panda/core/SwapchainGenerationMock.hpp"
#include "panda/core/WindowSurfaceMock.hpp"
#include "presentation/SwapchainGeneration.hpp"

namespace panda::test
{
using testing::_;
using testing::NotNull;
using testing::Return;
using testing::Sequence;
using testing::StrEq;

inline void expectColorAttachmentStages(const VkImageMemoryBarrier2& barrier)
{
    EXPECT_EQ(barrier.srcStageMask,
              static_cast<VkPipelineStageFlags2>(vk::PipelineStageFlagBits2::eColorAttachmentOutput));
    EXPECT_EQ(barrier.srcAccessMask, static_cast<VkAccessFlags2>(vk::AccessFlagBits2::eNone));
    EXPECT_EQ(barrier.dstStageMask,
              static_cast<VkPipelineStageFlags2>(vk::PipelineStageFlagBits2::eColorAttachmentOutput));
    EXPECT_EQ(barrier.dstAccessMask, static_cast<VkAccessFlags2>(vk::AccessFlagBits2::eColorAttachmentWrite));
}

inline void expectColorAttachmentLayouts(const VkImageMemoryBarrier2& barrier)
{
    EXPECT_EQ(barrier.oldLayout, static_cast<VkImageLayout>(vk::ImageLayout::eUndefined));
    EXPECT_EQ(barrier.newLayout, static_cast<VkImageLayout>(vk::ImageLayout::eColorAttachmentOptimal));
}

inline void expectPresentStages(const VkImageMemoryBarrier2& barrier)
{
    EXPECT_EQ(barrier.srcStageMask,
              static_cast<VkPipelineStageFlags2>(vk::PipelineStageFlagBits2::eColorAttachmentOutput));
    EXPECT_EQ(barrier.srcAccessMask, static_cast<VkAccessFlags2>(vk::AccessFlagBits2::eColorAttachmentWrite));
    EXPECT_EQ(barrier.dstStageMask, static_cast<VkPipelineStageFlags2>(vk::PipelineStageFlagBits2::eNone));
    EXPECT_EQ(barrier.dstAccessMask, static_cast<VkAccessFlags2>(vk::AccessFlagBits2::eNone));
}

inline void expectPresentLayouts(const VkImageMemoryBarrier2& barrier)
{
    EXPECT_EQ(barrier.oldLayout, static_cast<VkImageLayout>(vk::ImageLayout::eColorAttachmentOptimal));
    EXPECT_EQ(barrier.newLayout, static_cast<VkImageLayout>(vk::ImageLayout::ePresentSrcKHR));
}

inline void expectTransitionToColorAttachment(const VkDependencyInfo* dependency)
{
    ASSERT_EQ(dependency->imageMemoryBarrierCount, 1U);
    ASSERT_NE(dependency->pImageMemoryBarriers, nullptr);
    expectColorAttachmentStages(*dependency->pImageMemoryBarriers);
    expectColorAttachmentLayouts(*dependency->pImageMemoryBarriers);
}

inline void expectTransitionToPresent(const VkDependencyInfo* dependency)
{
    ASSERT_EQ(dependency->imageMemoryBarrierCount, 1U);
    ASSERT_NE(dependency->pImageMemoryBarriers, nullptr);
    expectPresentStages(*dependency->pImageMemoryBarriers);
    expectPresentLayouts(*dependency->pImageMemoryBarriers);
}

inline auto makeGeneration(const vk::raii::Device& device, std::uintptr_t handle)
    -> std::expected<panda::core::vulkan::SwapchainGeneration, panda::Error>
{
    auto images = std::vector<panda::core::vulkan::SwapchainImage> {};
    images.emplace_back(panda::core::vulkan::SwapchainImage {
        .image = vk::Image {fakeVulkanHandle<VkImage>(handle + 1U)},
        .view = {nullptr},
        .renderFinished = vk::raii::Semaphore {device, fakeVulkanHandle<VkSemaphore>(handle + 2U)},
        .presentFence = vk::raii::Fence {device, fakeVulkanHandle<VkFence>(handle + 3U)},
        .presentPending = false,
        .initialized = false
    });
    return panda::core::vulkan::SwapchainGeneration {
        .swapchain = vk::raii::SwapchainKHR {device,       fakeVulkanHandle<VkSwapchainKHR>(handle)},
        .images = std::move(images),
        .extent = vk::Extent2D {.width = 640, .height = 480                           },
        .format = vk::Format::eB8G8R8A8Srgb
    };
}

class PresentationTestSupport : public testing::Test
{
protected:
    void TearDown() override
    {
        if (_device)
        {
            std::ignore = _device->release();
        }
        std::ignore = _instance.release();
    }

    struct ResourceDestructionCounts
    {
        std::size_t semaphores {};
        std::size_t swapchains {};
    };

    auto getDevice() -> const vk::raii::Device&
    {
        if (!_device)
        {
            const auto physicalDevice = vk::raii::PhysicalDevice {_instance, fakeVulkanHandle<VkPhysicalDevice>(1)};
            _device.emplace(physicalDevice, fakeVulkanHandle<VkDevice>(1));
        }
        return *_device;
    }

    void expectReleaseEntryPoint()
    {
        EXPECT_CALL(vulkan, vkGetDeviceProcAddr(_, StrEq("vkReleaseSwapchainImagesKHR")))
            .Times(1)
            .WillRepeatedly([](VkDevice, const char*) {
                return panda::test::genericVulkanProc(&vkReleaseSwapchainImagesKHR);
            });
    }

    void expectNativeFrameResources(std::size_t semaphoreCount)
    {
        EXPECT_CALL(vulkan, vkCreateSemaphore(_, NotNull(), _, NotNull()))
            .Times(static_cast<int>(semaphoreCount))
            .WillRepeatedly(
                [this](VkDevice, const VkSemaphoreCreateInfo*, const VkAllocationCallbacks*, VkSemaphore* out) {
                    *out = fakeVulkanHandle<VkSemaphore>(_nextHandle++);
                    return static_cast<VkResult>(vk::Result::eSuccess);
                });
        EXPECT_CALL(vulkan, vkCreateCommandPool(_, NotNull(), _, NotNull()))
            .Times(2)
            .WillRepeatedly(
                [this](VkDevice, const VkCommandPoolCreateInfo*, const VkAllocationCallbacks*, VkCommandPool* out) {
                    *out = fakeVulkanHandle<VkCommandPool>(_nextHandle++);
                    return static_cast<VkResult>(vk::Result::eSuccess);
                });
        EXPECT_CALL(vulkan, vkAllocateCommandBuffers(_, NotNull(), NotNull()))
            .Times(2)
            .WillRepeatedly([this](VkDevice, const VkCommandBufferAllocateInfo*, VkCommandBuffer* out) {
                *out = fakeVulkanHandle<VkCommandBuffer>(_nextHandle++);
                return static_cast<VkResult>(vk::Result::eSuccess);
            });
    }

    void expectNativeResourceDestruction(ResourceDestructionCounts counts)
    {
        EXPECT_CALL(vulkan, vkDestroySemaphore(_, _, _)).Times(static_cast<int>(counts.semaphores));
        EXPECT_CALL(vulkan, vkDestroyCommandPool(_, _, _)).Times(2);
        EXPECT_CALL(vulkan, vkDestroySwapchainKHR(_, _, _)).Times(static_cast<int>(counts.swapchains));
        EXPECT_CALL(vulkan, vkDestroyFence(_, _, _)).Times(static_cast<int>(counts.swapchains));
    }

    void expectExtentReads(std::size_t count) const
    {
        EXPECT_CALL(window, getFramebufferExtent()).Times(static_cast<int>(count)).WillRepeatedly([] {
            return panda::test::WindowSurfaceMock::ExtentResult {
                panda::FramebufferExtent {.width = 640, .height = 480}
            };
        });
    }

    void expectInitialGeneration(std::uintptr_t handle)
    {
        EXPECT_CALL(swapchains, createSwapchainGeneration(_))
            .WillOnce(testing::Action<decltype(panda::core::vulkan::createSwapchainGeneration)> {
                [handle](const panda::core::vulkan::SwapchainGenerationRequest& request) {
                    EXPECT_EQ(request.oldSwapchain, vk::SwapchainKHR {});
                    return makeGeneration(request.device, handle);
                }});
    }

    void expectAcquiredImage(Sequence& flow, vk::Result result)
    {
        EXPECT_CALL(vulkan, vkAcquireNextImageKHR(_, _, _, _, _, NotNull()))
            .InSequence(flow)
            .WillOnce(testing::Action<decltype(vkAcquireNextImageKHR)> {
                [result](VkDevice, VkSwapchainKHR, std::uint64_t, VkSemaphore, VkFence, std::uint32_t* index) {
                    if (result == vk::Result::eSuccess || result == vk::Result::eSuboptimalKHR)
                    {
                        *index = 0;
                    }
                    else
                    {
                        EXPECT_EQ(*index, std::numeric_limits<std::uint32_t>::max());
                    }
                    return static_cast<VkResult>(result);
                }});
    }

    void expectCommandPoolResetFailure(Sequence& flow)
    {
        EXPECT_CALL(vulkan, vkResetCommandPool(_, _, _))
            .InSequence(flow)
            .WillOnce(Return(static_cast<VkResult>(vk::Result::eErrorInitializationFailed)));
    }

    void expectCommandRecording(Sequence& flow)
    {
        EXPECT_CALL(vulkan, vkResetCommandPool(_, _, _))
            .InSequence(flow)
            .WillOnce(Return(static_cast<VkResult>(vk::Result::eSuccess)));
        EXPECT_CALL(vulkan, vkBeginCommandBuffer(_, NotNull()))
            .InSequence(flow)
            .WillOnce(Return(static_cast<VkResult>(vk::Result::eSuccess)));
        EXPECT_CALL(vulkan, vkCmdPipelineBarrier2(_, NotNull()))
            .InSequence(flow)
            .WillOnce(testing::Action<decltype(vkCmdPipelineBarrier2)> {
                [](VkCommandBuffer, const VkDependencyInfo* dependency) {
                    expectTransitionToColorAttachment(dependency);
                }});
        EXPECT_CALL(vulkan, vkCmdBeginRendering(_, NotNull())).InSequence(flow);
        EXPECT_CALL(vulkan, vkCmdEndRendering(_)).InSequence(flow);
        EXPECT_CALL(vulkan, vkCmdPipelineBarrier2(_, NotNull()))
            .InSequence(flow)
            .WillOnce(testing::Action<decltype(vkCmdPipelineBarrier2)> {
                [](VkCommandBuffer, const VkDependencyInfo* dependency) {
                    expectTransitionToPresent(dependency);
                }});
        EXPECT_CALL(vulkan, vkEndCommandBuffer(_))
            .InSequence(flow)
            .WillOnce(Return(static_cast<VkResult>(vk::Result::eSuccess)));
    }

    void expectPresentFailure(Sequence& flow, vk::Result result = vk::Result::eErrorUnknown)
    {
        EXPECT_CALL(vulkan, vkQueuePresentKHR(_, NotNull()))
            .InSequence(flow)
            .WillOnce(Return(static_cast<VkResult>(result)));
    }

    void expectImageRelease(const Sequence& flow)
    {
        EXPECT_CALL(vulkan, vkReleaseSwapchainImagesKHR(_, NotNull()))
            .InSequence(flow)
            .WillOnce(testing::Action<decltype(vkReleaseSwapchainImagesKHR)> {
                [](VkDevice, const VkReleaseSwapchainImagesInfoKHR* releaseInfo) {
                    EXPECT_EQ(releaseInfo->imageIndexCount, 1U);
                    if (releaseInfo->imageIndexCount != 1U || releaseInfo->pImageIndices == nullptr)
                    {
                        ADD_FAILURE() << "Image release must contain one index";
                        return static_cast<VkResult>(vk::Result::eErrorUnknown);
                    }
                    EXPECT_EQ(*releaseInfo->pImageIndices, 0U);
                    return static_cast<VkResult>(vk::Result::eSuccess);
                }});
    }

    void expectFailedRecreation(const Sequence& flow, std::uintptr_t oldHandle)
    {
        EXPECT_CALL(swapchains, createSwapchainGeneration(_))
            .InSequence(flow)
            .WillOnce(testing::Action<decltype(panda::core::vulkan::createSwapchainGeneration)> {
                [oldHandle](const panda::core::vulkan::SwapchainGenerationRequest& request)
                    -> std::expected<panda::core::vulkan::SwapchainGeneration, panda::Error> {
                    EXPECT_EQ(request.oldSwapchain, vk::SwapchainKHR {fakeVulkanHandle<VkSwapchainKHR>(oldHandle)});
                    return std::unexpected {panda::makeError(panda::ErrorCode::BackendFailure, "replacement failed")};
                }});
    }

    void expectRetriedGeneration(Sequence& flow, std::uintptr_t handle)
    {
        EXPECT_CALL(swapchains, createSwapchainGeneration(_))
            .InSequence(flow)
            .WillOnce(testing::Action<decltype(panda::core::vulkan::createSwapchainGeneration)> {
                [handle](const panda::core::vulkan::SwapchainGenerationRequest& request) {
                    EXPECT_EQ(request.oldSwapchain, vk::SwapchainKHR {});
                    return makeGeneration(request.device, handle);
                }});
    }

    panda::test::ScopedMock<panda::test::VulkanMock> vulkan;
    testing::StrictMock<panda::test::WindowSurfaceMock> window;
    panda::test::ScopedMock<panda::test::SwapchainGenerationMock> swapchains;

private:
    vk::raii::Context _loader {vkGetInstanceProcAddr};
    vk::raii::Instance _instance {_loader, fakeVulkanHandle<VkInstance>(1)};
    std::optional<vk::raii::Device> _device;
    std::uintptr_t _nextHandle {100};
};
}
