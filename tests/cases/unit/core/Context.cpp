// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <cstdint>
#include <panda/Context.hpp>
#include <panda/Error.hpp>
#include <panda/Logger.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "LogCapture.hpp"
#include "ScopedMock.hpp"
#include "VulkanTestSupport.hpp"
#include "external/vulkan/VulkanMock.hpp"
#include "panda/core/SwapchainGenerationMock.hpp"
#include "panda/core/WindowSurfaceMock.hpp"

namespace
{
using panda::test::fakeVulkanHandle;
using testing::_;
using testing::DoAll;
using testing::NotNull;
using testing::Return;
using testing::Sequence;
using testing::SetArgPointee;
using testing::StrEq;

auto makeLayer(std::string_view name) -> VkLayerProperties
{
    auto layer = VkLayerProperties {};
    std::ranges::copy(name, std::span {layer.layerName}.begin());
    return layer;
}

auto makeExtension(std::string_view name) -> VkExtensionProperties
{
    auto extension = VkExtensionProperties {};
    std::ranges::copy(name, std::span {extension.extensionName}.begin());
    return extension;
}

template <typename Function>
auto genericVulkanProc(Function function) -> PFN_vkVoidFunction
{
    return reinterpret_cast<PFN_vkVoidFunction>(function);  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
}

void expectValidationPrerequisites(panda::test::VulkanMock& vulkan, bool debugUtilsAvailable = true)
{
    const auto validationLayer = makeLayer("VK_LAYER_KHRONOS_validation");
    const auto debugUtils = makeExtension(vk::EXTDebugUtilsExtensionName);
    const auto extensionCount = debugUtilsAvailable ? 1U : 0U;
    EXPECT_CALL(vulkan, vkEnumerateInstanceVersion(NotNull()))
        .WillOnce(DoAll(SetArgPointee<0>(vk::ApiVersion13), Return(static_cast<VkResult>(vk::Result::eSuccess))));
    EXPECT_CALL(vulkan, vkEnumerateInstanceLayerProperties(NotNull(), _))
        .WillRepeatedly([validationLayer](std::uint32_t* count, VkLayerProperties* properties) {
            *count = 1;
            if (properties != nullptr)
            {
                *properties = validationLayer;
            }
            return static_cast<VkResult>(vk::Result::eSuccess);
        });
    EXPECT_CALL(vulkan, vkEnumerateInstanceExtensionProperties(nullptr, NotNull(), _))
        .WillRepeatedly(
            [debugUtils, extensionCount](const char*, std::uint32_t* count, VkExtensionProperties* properties) {
                *count = extensionCount;
                if (properties != nullptr && extensionCount != 0)
                {
                    *properties = debugUtils;
                }
                return static_cast<VkResult>(vk::Result::eSuccess);
            });
}

void invokeValidationCallback(const VkDebugUtilsMessengerCreateInfoEXT& createInfo)
{
    ASSERT_NE(createInfo.pfnUserCallback, nullptr);
    const auto callbackData = VkDebugUtilsMessengerCallbackDataEXT {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CALLBACK_DATA_EXT,
        .pNext = nullptr,
        .flags = 0,
        .pMessageIdName = nullptr,
        .messageIdNumber = 0,
        .pMessage = "mock validation warning",
        .queueLabelCount = 0,
        .pQueueLabels = nullptr,
        .cmdBufLabelCount = 0,
        .pCmdBufLabels = nullptr,
        .objectCount = 0,
        .pObjects = nullptr,
    };
    const auto result = createInfo.pfnUserCallback(VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT,
                                                   VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT,
                                                   &callbackData,
                                                   nullptr);
    EXPECT_EQ(result, VkBool32 {});
}

void expectValidationInstanceCreated(panda::test::VulkanMock& vulkan, VkInstance instance)
{
    EXPECT_CALL(vulkan, vkCreateInstance(NotNull(), nullptr, NotNull()))
        .WillOnce([instance](const VkInstanceCreateInfo* createInfo, const VkAllocationCallbacks*, VkInstance* output) {
            EXPECT_EQ(createInfo->enabledLayerCount, 1U);
            EXPECT_NE(createInfo->pNext, nullptr);
            if (createInfo->pNext == nullptr)
            {
                return static_cast<VkResult>(vk::Result::eErrorInitializationFailed);
            }
            const auto* debugInfo = static_cast<const VkDebugUtilsMessengerCreateInfoEXT*>(createInfo->pNext);
            EXPECT_NE(debugInfo->pfnUserCallback, nullptr);
            const auto extensionNames =
                std::span {createInfo->ppEnabledExtensionNames, createInfo->enabledExtensionCount};
            const auto hasDebugUtils = std::ranges::any_of(extensionNames, [](const char* extensionName) {
                return std::string_view {extensionName} == vk::EXTDebugUtilsExtensionName;
            });
            EXPECT_TRUE(hasDebugUtils);
            *output = instance;
            return static_cast<VkResult>(vk::Result::eSuccess);
        });
}

void expectValidationMessengerCreated(panda::test::VulkanMock& vulkan,
                                      VkInstance instance,
                                      VkDebugUtilsMessengerEXT messenger)
{
    auto lookupOrder = Sequence {};
    EXPECT_CALL(vulkan, vkGetInstanceProcAddr(instance, StrEq("vkCreateDebugUtilsMessengerEXT")))
        .InSequence(lookupOrder)
        .WillOnce(Return(genericVulkanProc(&vkCreateDebugUtilsMessengerEXT)));
    EXPECT_CALL(vulkan, vkGetInstanceProcAddr(instance, StrEq("vkDestroyDebugUtilsMessengerEXT")))
        .InSequence(lookupOrder)
        .WillOnce(Return(genericVulkanProc(&vkDestroyDebugUtilsMessengerEXT)));
    EXPECT_CALL(vulkan, vkCreateDebugUtilsMessengerEXT(instance, NotNull(), nullptr, NotNull()))
        .WillOnce([messenger](VkInstance,
                              const VkDebugUtilsMessengerCreateInfoEXT* createInfo,
                              const VkAllocationCallbacks*,
                              VkDebugUtilsMessengerEXT* output) {
            invokeValidationCallback(*createInfo);
            *output = messenger;
            return static_cast<VkResult>(vk::Result::eSuccess);
        });
}

void expectValidationMessengerCreationFailure(panda::test::VulkanMock& vulkan,
                                              VkInstance instance,
                                              VkDebugUtilsMessengerEXT output)
{
    auto lookupOrder = Sequence {};
    EXPECT_CALL(vulkan, vkGetInstanceProcAddr(instance, StrEq("vkCreateDebugUtilsMessengerEXT")))
        .InSequence(lookupOrder)
        .WillOnce(Return(genericVulkanProc(&vkCreateDebugUtilsMessengerEXT)));
    EXPECT_CALL(vulkan, vkGetInstanceProcAddr(instance, StrEq("vkDestroyDebugUtilsMessengerEXT")))
        .InSequence(lookupOrder)
        .WillOnce(Return(genericVulkanProc(&vkDestroyDebugUtilsMessengerEXT)));
    EXPECT_CALL(vulkan, vkCreateDebugUtilsMessengerEXT(instance, NotNull(), nullptr, NotNull()))
        .WillOnce([output](VkInstance,
                           const VkDebugUtilsMessengerCreateInfoEXT*,
                           const VkAllocationCallbacks*,
                           VkDebugUtilsMessengerEXT* handle) {
            *handle = output;
            return static_cast<VkResult>(vk::Result::eErrorInitializationFailed);
        });
    EXPECT_CALL(vulkan, vkDestroyDebugUtilsMessengerEXT(_, _, _)).Times(0);
    EXPECT_CALL(vulkan, vkDestroyInstance(instance, nullptr));
}

void expectNoPhysicalDevices(panda::test::VulkanMock& vulkan, VkInstance instance, VkDebugUtilsMessengerEXT messenger)
{
    auto lifetime = Sequence {};
    EXPECT_CALL(vulkan, vkDestroyDebugUtilsMessengerEXT(instance, messenger, nullptr)).InSequence(lifetime);
    EXPECT_CALL(vulkan, vkDestroyInstance(instance, nullptr)).InSequence(lifetime);
    EXPECT_CALL(vulkan, vkEnumeratePhysicalDevices(instance, NotNull(), _))
        .WillOnce([](VkInstance, std::uint32_t* count, VkPhysicalDevice*) {
            *count = 0;
            return static_cast<VkResult>(vk::Result::eSuccess);
        });
}

class ContextTest : public testing::Test
{
protected:
    panda::test::ScopedMock<panda::test::VulkanMock> vulkan;
    panda::test::ScopedMock<panda::test::SwapchainGenerationMock> swapchain;
};
}

TEST_F(ContextTest, RejectsLoaderVersionBelow13BeforeCreatingAnInstance)
{
    EXPECT_CALL(vulkan, vkEnumerateInstanceVersion(NotNull()))
        .WillOnce(DoAll(SetArgPointee<0>(vk::ApiVersion12), Return(static_cast<VkResult>(vk::Result::eSuccess))));
    const auto context = panda::Context::create();
    ASSERT_FALSE(context.has_value());
    EXPECT_EQ(context.error().code, panda::ErrorCode::Unsupported);
    EXPECT_NE(context.error().message.find("1.3"), std::string::npos);
}

TEST_F(ContextTest, PreservesLoaderVersionQueryFailure)
{
    EXPECT_CALL(vulkan, vkEnumerateInstanceVersion(NotNull()))
        .WillOnce(Return(static_cast<VkResult>(vk::Result::eErrorInitializationFailed)));
    const auto context = panda::Context::create();
    ASSERT_FALSE(context.has_value());
    EXPECT_EQ(context.error().code, panda::ErrorCode::BackendFailure);
    ASSERT_TRUE(context.error().native.has_value());
    const auto native = context.error().native.value_or(panda::NativeError {.api = {}, .code = 0});
    EXPECT_EQ(native.api, "Vulkan");
    EXPECT_EQ(native.code, static_cast<VkResult>(vk::Result::eErrorInitializationFailed));
}

TEST_F(ContextTest, DoesNotDestroyInstanceWhenCreationFails)
{
    EXPECT_CALL(vulkan, vkEnumerateInstanceVersion(NotNull()))
        .WillOnce(DoAll(SetArgPointee<0>(vk::ApiVersion13), Return(static_cast<VkResult>(vk::Result::eSuccess))));
    EXPECT_CALL(vulkan, vkCreateInstance(NotNull(), nullptr, NotNull()))
        .WillOnce([](const VkInstanceCreateInfo*, const VkAllocationCallbacks*, VkInstance* output) {
            *output = fakeVulkanHandle<VkInstance>(1);
            return static_cast<VkResult>(vk::Result::eErrorInitializationFailed);
        });
    EXPECT_CALL(vulkan, vkDestroyInstance(_, _)).Times(0);
    EXPECT_CALL(vulkan, vkGetInstanceProcAddr(fakeVulkanHandle<VkInstance>(1), _)).Times(0);

    const auto context = panda::Context::create();

    ASSERT_FALSE(context.has_value());
    EXPECT_EQ(context.error().code, panda::ErrorCode::BackendFailure);
}

TEST_F(ContextTest, ReportsMissingRequestedValidationLayer)
{
    EXPECT_CALL(vulkan, vkEnumerateInstanceVersion(NotNull()))
        .WillOnce(DoAll(SetArgPointee<0>(vk::ApiVersion13), Return(static_cast<VkResult>(vk::Result::eSuccess))));
    EXPECT_CALL(vulkan, vkEnumerateInstanceLayerProperties(NotNull(), _))
        .WillRepeatedly([](std::uint32_t* count, [[maybe_unused]] VkLayerProperties* properties) {
            *count = 0;
            return static_cast<VkResult>(vk::Result::eSuccess);
        });
    const auto context = panda::Context::create({.enableValidation = true});
    ASSERT_FALSE(context.has_value());
    EXPECT_EQ(context.error().code, panda::ErrorCode::Unsupported);
    EXPECT_NE(context.error().message.find("VK_LAYER_KHRONOS_validation"), std::string::npos);
}

TEST_F(ContextTest, ReportsMissingDebugUtilsExtensionWhenValidationIsRequested)
{
    expectValidationPrerequisites(vulkan, false);

    const auto context = panda::Context::create({.enableValidation = true});

    ASSERT_FALSE(context.has_value());
    EXPECT_EQ(context.error().code, panda::ErrorCode::Unsupported);
    EXPECT_NE(context.error().message.find("VK_EXT_debug_utils"), std::string::npos);
}

TEST_F(ContextTest, EnablesValidationCallbackWithoutEnvironmentConfiguration)
{
    auto logRecords = panda::test::LogRecords {};
    auto logSink = panda::test::ProcessSinkRegistration {logRecords};
    auto* const instance = fakeVulkanHandle<VkInstance>(1);
    auto* const messenger = fakeVulkanHandle<VkDebugUtilsMessengerEXT>(2);
    expectValidationPrerequisites(vulkan);
    expectValidationInstanceCreated(vulkan, instance);
    expectValidationMessengerCreated(vulkan, instance, messenger);
    expectNoPhysicalDevices(vulkan, instance, messenger);

    const auto context = panda::Context::create({.enableValidation = true});

    ASSERT_FALSE(context.has_value());
    EXPECT_EQ(context.error().code, panda::ErrorCode::Unsupported);
    ASSERT_EQ(logRecords.entries.size(), 1U);
    EXPECT_EQ(logRecords.entries.front().level, panda::log::Level::Warning);
    EXPECT_NE(logRecords.entries.front().message.find("mock validation warning"), std::string::npos);
}

TEST_F(ContextTest, DoesNotDestroyMessengerWhenCreationFails)
{
    auto* const instance = fakeVulkanHandle<VkInstance>(1);
    auto* const output = fakeVulkanHandle<VkDebugUtilsMessengerEXT>(2);
    expectValidationPrerequisites(vulkan);
    expectValidationInstanceCreated(vulkan, instance);
    expectValidationMessengerCreationFailure(vulkan, instance, output);

    const auto context = panda::Context::create({.enableValidation = true});

    ASSERT_FALSE(context.has_value());
    EXPECT_EQ(context.error().code, panda::ErrorCode::BackendFailure);
}

TEST_F(ContextTest, ReportsMissingDebugMessengerEntryPointBeforeCreation)
{
    auto* const instance = fakeVulkanHandle<VkInstance>(1);
    expectValidationPrerequisites(vulkan);
    expectValidationInstanceCreated(vulkan, instance);
    EXPECT_CALL(vulkan, vkGetInstanceProcAddr(instance, StrEq("vkCreateDebugUtilsMessengerEXT")))
        .WillOnce(Return(nullptr));
    EXPECT_CALL(vulkan, vkCreateDebugUtilsMessengerEXT(_, _, _, _)).Times(0);
    EXPECT_CALL(vulkan, vkDestroyInstance(instance, nullptr));

    const auto context = panda::Context::create({.enableValidation = true});

    ASSERT_FALSE(context.has_value());
    EXPECT_EQ(context.error().code, panda::ErrorCode::Unsupported);
    EXPECT_NE(context.error().message.find("VK_EXT_debug_utils"), std::string::npos);
}

TEST_F(ContextTest, RejectsWindowWithoutSurfaceExtensionBeforeNativeCreation)
{
    auto surface = testing::StrictMock<panda::test::WindowSurfaceMock> {};
    EXPECT_CALL(vulkan, vkEnumerateInstanceVersion(NotNull()))
        .WillOnce(DoAll(SetArgPointee<0>(vk::ApiVersion13), Return(static_cast<VkResult>(vk::Result::eSuccess))));
    const auto extensions =
        panda::test::WindowSurfaceMock::ExtensionResult {std::vector<std::string> {"VK_EXT_debug_utils"}};
    EXPECT_CALL(surface, getRequiredInstanceExtensions()).WillOnce(Return(extensions));
    const auto context = panda::Context::createWithSurface(surface);
    ASSERT_FALSE(context.has_value());
    EXPECT_EQ(context.error().code, panda::ErrorCode::InvalidArgument);
    EXPECT_NE(context.error().message.find("VK_KHR_surface"), std::string::npos);
}
