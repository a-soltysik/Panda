// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <expected>
#include <panda/Context.hpp>
#include <panda/Error.hpp>
#include <panda/WindowSurface.hpp>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ScopedMock.hpp"
#include "SwapchainGeneration.hpp"
#include "VulkanTestSupport.hpp"
#include "external/vulkan/VulkanMock.hpp"
#include "panda/core/SwapchainGenerationMock.hpp"
#include "panda/core/WindowSurfaceMock.hpp"

namespace
{
using panda::test::fakeVulkanHandle;
using testing::_;
using testing::NotNull;
using testing::Return;

constexpr auto success = static_cast<VkResult>(vk::Result::eSuccess);
constexpr auto exhausted = static_cast<VkResult>(vk::Result::eErrorOutOfDeviceMemory);

auto extension(std::string_view name) -> VkExtensionProperties
{
    auto value = VkExtensionProperties {};
    std::ranges::copy(name, std::span {value.extensionName}.begin());
    return value;
}

void verifyDeviceFeatures(const VkDeviceCreateInfo& info, bool windowed)
{
    ASSERT_NE(info.pNext, nullptr);
    const auto* const features = static_cast<const VkPhysicalDeviceFeatures2*>(info.pNext);
    ASSERT_NE(features->pNext, nullptr);
    const auto* const vulkan12 = static_cast<const VkPhysicalDeviceVulkan12Features*>(features->pNext);
    ASSERT_NE(vulkan12->pNext, nullptr);
    const auto* const vulkan13 = static_cast<const VkPhysicalDeviceVulkan13Features*>(vulkan12->pNext);
    EXPECT_EQ(vulkan12->timelineSemaphore, vk::True);
    EXPECT_EQ(vulkan13->dynamicRendering, vk::True);
    EXPECT_EQ(vulkan13->synchronization2, vk::True);
    if (windowed)
    {
        ASSERT_NE(vulkan13->pNext, nullptr);
        const auto* const maintenance =
            static_cast<const VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR*>(vulkan13->pNext);
        EXPECT_EQ(maintenance->swapchainMaintenance1, vk::True);
    }
    else
    {
        EXPECT_EQ(vulkan13->pNext, nullptr);
    }
}

class ContextLifecycleTest : public testing::Test
{
protected:
    void expectInstance()
    {
        EXPECT_CALL(vulkan, vkEnumerateInstanceVersion(NotNull())).WillOnce([](std::uint32_t* version) {
            *version = vk::ApiVersion13;
            return success;
        });
        EXPECT_CALL(vulkan, vkCreateInstance(NotNull(), nullptr, NotNull()))
            .WillOnce([this](const VkInstanceCreateInfo* info, const VkAllocationCallbacks*, VkInstance* output) {
                EXPECT_EQ(info->pApplicationInfo->apiVersion, vk::ApiVersion13);
                EXPECT_EQ(info->enabledLayerCount, 0U);
                *output = instance;
                return success;
            });
    }

    void expectAdapter(bool windowed, bool separateQueues = false)
    {
        expectEnumeration();
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceFeatures2(physical, NotNull()))
            .WillOnce([windowed](VkPhysicalDevice, VkPhysicalDeviceFeatures2* features) {
                auto* const vulkan12 = static_cast<VkPhysicalDeviceVulkan12Features*>(features->pNext);
                auto* const vulkan13 = static_cast<VkPhysicalDeviceVulkan13Features*>(vulkan12->pNext);
                vulkan12->timelineSemaphore = vk::True;
                vulkan13->dynamicRendering = vk::True;
                vulkan13->synchronization2 = vk::True;
                if (windowed)
                {
                    auto* const maintenance =
                        static_cast<VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR*>(vulkan13->pNext);
                    maintenance->swapchainMaintenance1 = vk::True;
                }
            });
        expectQueueFamilies(separateQueues);
        if (windowed)
        {
            expectPresentationSupport(separateQueues);
        }
    }

    void expectEnumeration()
    {
        EXPECT_CALL(vulkan, vkEnumeratePhysicalDevices(instance, NotNull(), _))
            .Times(2)
            .WillRepeatedly([this](VkInstance, std::uint32_t* count, VkPhysicalDevice* output) {
                *count = 1;
                if (output != nullptr)
                {
                    *output = physical;
                }
                return success;
            });
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceProperties(physical, NotNull()))
            .Times(2)
            .WillRepeatedly([](VkPhysicalDevice, VkPhysicalDeviceProperties* output) {
                *output = VkPhysicalDeviceProperties {};
                output->apiVersion = vk::ApiVersion13;
                output->deviceType = static_cast<VkPhysicalDeviceType>(vk::PhysicalDeviceType::eDiscreteGpu);
                std::ranges::copy(std::string_view {"Unit GPU"}, std::span {output->deviceName}.begin());
            });
    }

    void expectQueueFamilies(bool separate)
    {
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceQueueFamilyProperties(physical, NotNull(), _))
            .Times(2)
            .WillRepeatedly([separate](VkPhysicalDevice, std::uint32_t* count, VkQueueFamilyProperties* output) {
                *count = separate ? 2U : 1U;
                if (output != nullptr)
                {
                    auto families = std::span {output, *count};
                    families.front() = VkQueueFamilyProperties {
                        .queueFlags =
                            static_cast<VkQueueFlags>(vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute),
                        .queueCount = 1,
                        .timestampValidBits = 0,
                        .minImageTransferGranularity = {.width = 1, .height = 1, .depth = 1}
                    };
                    if (separate)
                    {
                        families.back() = VkQueueFamilyProperties {
                            .queueFlags = static_cast<VkQueueFlags>(vk::QueueFlagBits::eTransfer),
                            .queueCount = 1,
                            .timestampValidBits = 0,
                            .minImageTransferGranularity = {.width = 1, .height = 1, .depth = 1}
                        };
                    }
                }
            });
    }

    void expectPresentationSupport(bool separate)
    {
        EXPECT_CALL(vulkan, vkEnumerateDeviceExtensionProperties(physical, nullptr, NotNull(), _))
            .Times(2)
            .WillRepeatedly([](VkPhysicalDevice, const char*, std::uint32_t* count, VkExtensionProperties* output) {
                const auto entries = std::array {extension(vk::KHRSwapchainExtensionName),
                                                 extension(vk::KHRSwapchainMaintenance1ExtensionName)};
                *count = static_cast<std::uint32_t>(entries.size());
                if (output != nullptr)
                {
                    std::ranges::copy(entries, output);
                }
                return success;
            });
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceSurfaceSupportKHR(physical, 0, surface, NotNull()))
            .WillOnce([separate](VkPhysicalDevice, std::uint32_t, VkSurfaceKHR, VkBool32* supported) {
                *supported = separate ? vk::False : vk::True;
                return success;
            });
        if (separate)
        {
            EXPECT_CALL(vulkan, vkGetPhysicalDeviceSurfaceSupportKHR(physical, 1, surface, NotNull()))
                .WillOnce([](VkPhysicalDevice, std::uint32_t, VkSurfaceKHR, VkBool32* supported) {
                    *supported = vk::True;
                    return success;
                });
        }
        expectSurfaceFormats();
    }

    void expectSurfaceFormats()
    {
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, NotNull(), _))
            .Times(2)
            .WillRepeatedly([](VkPhysicalDevice, VkSurfaceKHR, std::uint32_t* count, VkSurfaceFormatKHR* output) {
                *count = 1;
                if (output != nullptr)
                {
                    *output = VkSurfaceFormatKHR {
                        .format = static_cast<VkFormat>(vk::Format::eB8G8R8A8Srgb),
                        .colorSpace = static_cast<VkColorSpaceKHR>(vk::ColorSpaceKHR::eSrgbNonlinear)};
                }
                return success;
            });
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, NotNull(), _))
            .Times(2)
            .WillRepeatedly([](VkPhysicalDevice, VkSurfaceKHR, std::uint32_t* count, VkPresentModeKHR* output) {
                *count = 1;
                if (output != nullptr)
                {
                    *output = static_cast<VkPresentModeKHR>(vk::PresentModeKHR::eFifo);
                }
                return success;
            });
    }

    void expectWindow(panda::test::WindowSurfaceMock::SurfaceResult result)
    {
        EXPECT_CALL(window, getRequiredInstanceExtensions())
            .WillOnce(Return(panda::test::WindowSurfaceMock::ExtensionResult {
                std::vector<std::string> {vk::KHRSurfaceExtensionName}}));
        EXPECT_CALL(vulkan, vkEnumerateInstanceExtensionProperties(nullptr, NotNull(), _))
            .Times(2)
            .WillRepeatedly([](const char*, std::uint32_t* count, VkExtensionProperties* output) {
                const auto entries = std::array {extension(vk::KHRSurfaceExtensionName),
                                                 extension(vk::KHRGetSurfaceCapabilities2ExtensionName),
                                                 extension(vk::KHRSurfaceMaintenance1ExtensionName)};
                *count = static_cast<std::uint32_t>(entries.size());
                if (output != nullptr)
                {
                    std::ranges::copy(entries, output);
                }
                return success;
            });
        EXPECT_CALL(window, createSurface(instance)).WillOnce(Return(std::move(result)));
    }

    void expectDevice(bool windowed, VkResult result = success, bool separateQueues = false)
    {
        EXPECT_CALL(vulkan, vkCreateDevice(physical, NotNull(), nullptr, NotNull()))
            .WillOnce([this, windowed, result, separateQueues](VkPhysicalDevice,
                                                               const VkDeviceCreateInfo* info,
                                                               const VkAllocationCallbacks*,
                                                               VkDevice* output) {
                verifyDeviceFeatures(*info, windowed);
                EXPECT_EQ(info->queueCreateInfoCount, separateQueues ? 2U : 1U);
                EXPECT_EQ(info->enabledExtensionCount, windowed ? 2U : 0U);
                if (windowed)
                {
                    const auto names = std::span {info->ppEnabledExtensionNames, info->enabledExtensionCount};
                    EXPECT_EQ(std::string_view {names.front()}, vk::KHRSwapchainExtensionName);
                    EXPECT_EQ(std::string_view {names.back()}, vk::KHRSwapchainMaintenance1ExtensionName);
                }
                *output = device;
                return result;
            });
    }

    void expectParentsDestroyed(bool hasDevice, bool hasSurface)
    {
        auto parents = testing::ExpectationSet {};
        if (hasDevice)
        {
            parents += EXPECT_CALL(vulkan, vkDestroyDevice(device, nullptr)).After(frameCleanup);
        }
        if (hasSurface)
        {
            parents += EXPECT_CALL(vulkan, vkDestroySurfaceKHR(instance, surface, nullptr));
        }
        EXPECT_CALL(vulkan, vkDestroyInstance(instance, nullptr)).After(parents);
    }

    void expectFrameResources()
    {
        EXPECT_CALL(vulkan, vkGetDeviceQueue(device, _, 0, NotNull()))
            .Times(2)
            .WillRepeatedly([](VkDevice, std::uint32_t, std::uint32_t, VkQueue* output) {
                *output = fakeVulkanHandle<VkQueue>(5);
            });
        EXPECT_CALL(vulkan, vkCreateSemaphore(device, NotNull(), nullptr, NotNull()))
            .WillOnce(testing::DoAll(testing::SetArgPointee<3>(fakeVulkanHandle<VkSemaphore>(10)), Return(success)))
            .WillOnce(testing::DoAll(testing::SetArgPointee<3>(fakeVulkanHandle<VkSemaphore>(11)), Return(success)))
            .WillOnce(testing::DoAll(testing::SetArgPointee<3>(fakeVulkanHandle<VkSemaphore>(12)), Return(success)));
        EXPECT_CALL(vulkan, vkCreateCommandPool(device, NotNull(), nullptr, NotNull()))
            .WillOnce(testing::DoAll(testing::SetArgPointee<3>(fakeVulkanHandle<VkCommandPool>(20)), Return(success)))
            .WillOnce(testing::DoAll(testing::SetArgPointee<3>(fakeVulkanHandle<VkCommandPool>(21)), Return(success)));
        EXPECT_CALL(vulkan, vkAllocateCommandBuffers(device, NotNull(), NotNull()))
            .WillOnce(testing::DoAll(testing::SetArgPointee<2>(fakeVulkanHandle<VkCommandBuffer>(30)), Return(success)))
            .WillOnce(
                testing::DoAll(testing::SetArgPointee<2>(fakeVulkanHandle<VkCommandBuffer>(31)), Return(success)));
    }

    void expectFramesDestroyed()
    {
        frameCleanup += EXPECT_CALL(vulkan, vkDestroySemaphore(device, fakeVulkanHandle<VkSemaphore>(12), nullptr));
        frameCleanup += EXPECT_CALL(vulkan, vkDestroyCommandPool(device, fakeVulkanHandle<VkCommandPool>(21), nullptr));
        frameCleanup += EXPECT_CALL(vulkan, vkDestroySemaphore(device, fakeVulkanHandle<VkSemaphore>(11), nullptr));
        frameCleanup += EXPECT_CALL(vulkan, vkDestroyCommandPool(device, fakeVulkanHandle<VkCommandPool>(20), nullptr));
        frameCleanup += EXPECT_CALL(vulkan, vkDestroySemaphore(device, fakeVulkanHandle<VkSemaphore>(10), nullptr));
    }

    void expectWindowed(bool separateQueues = false)
    {
        expectInstance();
        expectWindow(panda::test::WindowSurfaceMock::SurfaceResult {surface});
        expectAdapter(true, separateQueues);
        expectDevice(true, success, separateQueues);
        expectFrameResources();
        expectFramesDestroyed();
        expectParentsDestroyed(true, true);
    }

    panda::test::ScopedMock<panda::test::VulkanMock> vulkan;
    panda::test::ScopedMock<panda::test::SwapchainGenerationMock> generations;
    testing::StrictMock<panda::test::WindowSurfaceMock> window;
    VkInstance instance {fakeVulkanHandle<VkInstance>(1)};
    VkPhysicalDevice physical {fakeVulkanHandle<VkPhysicalDevice>(2)};
    VkDevice device {fakeVulkanHandle<VkDevice>(3)};
    VkSurfaceKHR surface {fakeVulkanHandle<VkSurfaceKHR>(4)};
    testing::ExpectationSet frameCleanup;
};
}

TEST_F(ContextLifecycleTest, CreatesMovesAndDestroysHeadlessDeviceInDependencyOrder)
{
    expectInstance();
    expectAdapter(false);
    expectDevice(false);
    expectParentsDestroyed(true, false);
    auto context = panda::Context::create();
    ASSERT_TRUE(context.has_value());
    auto moved = std::move(*context);
    EXPECT_FALSE(context->isValid());
    EXPECT_EQ(moved.getDeviceInfo().name, "Unit GPU");
    EXPECT_EQ(moved.getDeviceInfo().queueFamily, 0U);
    const auto frame = moved.presentClearFrame();
    ASSERT_FALSE(frame.has_value());
    EXPECT_EQ(frame.error().code, panda::ErrorCode::Unsupported);
}

TEST_F(ContextLifecycleTest, DeviceCreationFailurePreservesNativeErrorWithoutAdoptingTheOutput)
{
    expectInstance();
    expectAdapter(false);
    expectDevice(false, exhausted);
    expectParentsDestroyed(false, false);
    EXPECT_CALL(vulkan, vkDestroyDevice(_, _)).Times(0);
    EXPECT_CALL(vulkan, vkGetDeviceProcAddr(device, _)).Times(0);
    const auto context = panda::Context::create();
    ASSERT_FALSE(context.has_value());
    EXPECT_EQ(context.error().code, panda::ErrorCode::ResourceExhausted);
    ASSERT_TRUE(context.error().native.has_value());
    EXPECT_EQ(context.error().native.value_or(panda::NativeError {.api = {}, .code = 0}).code, exhausted);
}

TEST_F(ContextLifecycleTest, FailedSurfaceCreationReleasesInstanceAndPreservesFacadeError)
{
    expectInstance();
    expectWindow(std::unexpected {panda::makeError(panda::ErrorCode::SurfaceLost, "surface lost")});
    expectParentsDestroyed(false, false);
    const auto context = panda::Context::createWithSurface(window);
    ASSERT_FALSE(context.has_value());
    EXPECT_EQ(context.error().code, panda::ErrorCode::SurfaceLost);
    EXPECT_EQ(context.error().message, "surface lost");
}

TEST_F(ContextLifecycleTest, NullSurfaceIsRejectedBeforeDeviceSelection)
{
    expectInstance();
    expectWindow(panda::test::WindowSurfaceMock::SurfaceResult {VkSurfaceKHR {}});
    expectParentsDestroyed(false, false);
    const auto context = panda::Context::createWithSurface(window);
    ASSERT_FALSE(context.has_value());
    EXPECT_EQ(context.error().code, panda::ErrorCode::BackendFailure);
}

TEST_F(ContextLifecycleTest, WindowedDeviceFailureReleasesSurfaceBeforeInstance)
{
    expectInstance();
    expectWindow(panda::test::WindowSurfaceMock::SurfaceResult {surface});
    expectAdapter(true);
    expectDevice(true, exhausted);
    expectParentsDestroyed(false, true);
    const auto context = panda::Context::createWithSurface(window);
    ASSERT_FALSE(context.has_value());
    EXPECT_EQ(context.error().code, panda::ErrorCode::ResourceExhausted);
}

TEST_F(ContextLifecycleTest, MovedSuspendedContextRetainsItsDeviceUntilAllFrameResourcesAreDestroyed)
{
    expectWindowed();
    EXPECT_CALL(window, getFramebufferExtent())
        .Times(2)
        .WillRepeatedly(Return(panda::test::WindowSurfaceMock::ExtentResult {panda::FramebufferExtent {}}));
    auto context = panda::Context::createWithSurface(window);
    ASSERT_TRUE(context.has_value());
    auto moved = std::move(*context);
    const auto frame = moved.presentClearFrame();
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->status, panda::FrameResult::Status::Suspended);
}

TEST_F(ContextLifecycleTest, SeparatePresentFamilyProducesTwoDeviceQueueRequests)
{
    expectWindowed(true);
    EXPECT_CALL(window, getFramebufferExtent())
        .WillOnce(Return(panda::test::WindowSurfaceMock::ExtentResult {panda::FramebufferExtent {}}));
    const auto context = panda::Context::createWithSurface(window);
    ASSERT_TRUE(context.has_value());
    EXPECT_EQ(context->getDeviceInfo().queueFamily, 0U);
    EXPECT_EQ(context->getDeviceInfo().presentQueueFamily, 1U);
}

TEST_F(ContextLifecycleTest, ExtentQueryFailureReleasesFrameResourcesAndAllParents)
{
    expectWindowed();
    EXPECT_CALL(window, getFramebufferExtent())
        .WillOnce(Return(panda::test::WindowSurfaceMock::ExtentResult {
            std::unexpected {panda::makeError(panda::ErrorCode::BackendFailure, "extent failed")}}));
    const auto context = panda::Context::createWithSurface(window);
    ASSERT_FALSE(context.has_value());
    EXPECT_EQ(context.error().message, "extent failed");
}

TEST_F(ContextLifecycleTest, GenerationFailureReleasesFrameResourcesAndAllParents)
{
    expectWindowed();
    EXPECT_CALL(window, getFramebufferExtent())
        .WillOnce(Return(panda::test::WindowSurfaceMock::ExtentResult {
            panda::FramebufferExtent {.width = 640, .height = 480}
    }));
    EXPECT_CALL(generations, createSwapchainGeneration(_, _, _, _, _, _))
        .WillOnce(Return(panda::Result<panda::detail::SwapchainGeneration> {
            std::unexpected {panda::makeError(panda::ErrorCode::ResourceExhausted, "generation failed")}}));
    const auto context = panda::Context::createWithSurface(window);
    ASSERT_FALSE(context.has_value());
    EXPECT_EQ(context.error().message, "generation failed");
}
