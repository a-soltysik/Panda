#include <gtest/gtest.h>

#include <cstdint>
#include <panda/base/Error.hpp>
#include <panda/vulkan/Error.hpp>
#include <string>
#include <utility>
#include <vulkan/vulkan.hpp>

TEST(VulkanError, StatusesMapToCallerRecoveryActionsAndRetainNativeCodes)
{
    const auto unsupported = panda::makeVulkanError(vk::Result::eErrorFeatureNotPresent);
    const auto invalidArgument = panda::makeVulkanError(vk::Result::eErrorInvalidExternalHandle);
    const auto exhausted = panda::makeVulkanError(vk::Result::eErrorOutOfDeviceMemory);
    const auto timeout = panda::makeVulkanError(vk::Result::eTimeout);
    const auto lostSurface = panda::makeVulkanError(vk::Result::eErrorSurfaceLostKHR);
    const auto lostDevice = panda::makeVulkanError(vk::Result::eErrorDeviceLost);
    const auto backendFailure = panda::makeVulkanError(vk::Result::eErrorInitializationFailed);

    EXPECT_EQ(unsupported.code, panda::ErrorCode::Unsupported);
    EXPECT_EQ(invalidArgument.code, panda::ErrorCode::InvalidArgument);
    EXPECT_EQ(exhausted.code, panda::ErrorCode::ResourceExhausted);
    EXPECT_EQ(timeout.code, panda::ErrorCode::Timeout);
    EXPECT_EQ(lostSurface.code, panda::ErrorCode::SurfaceLost);
    EXPECT_EQ(lostDevice.code, panda::ErrorCode::DeviceLost);
    EXPECT_EQ(backendFailure.code, panda::ErrorCode::BackendFailure);

    const auto errors =
        {&unsupported, &invalidArgument, &exhausted, &timeout, &lostSurface, &lostDevice, &backendFailure};
    for (const auto* error : errors)
    {
        ASSERT_TRUE(error->native.has_value());
        const auto native = error->native.value_or(panda::NativeError {.api = {}, .code = 0});
        EXPECT_EQ(native.api, "Vulkan");
        EXPECT_FALSE(error->message.empty());
    }
    const auto native = lostSurface.native.value_or(panda::NativeError {.api = {}, .code = 0});
    EXPECT_EQ(native.code, std::to_underlying(vk::Result::eErrorSurfaceLostKHR));
}

TEST(VulkanError, DefaultsUnrecognizedStatusToBackendFailure)
{
    static constexpr auto unknownStatus = std::int32_t {123456789};
    // Deliberate unknown backend status verifies the recovery fallback.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    const auto error = panda::makeVulkanError(static_cast<vk::Result>(unknownStatus));

    EXPECT_EQ(error.code, panda::ErrorCode::BackendFailure);
    EXPECT_NE(error.message.find("Vulkan operation failed"), std::string::npos);
    ASSERT_TRUE(error.native.has_value());
    const auto native = error.native.value_or(panda::NativeError {.api = {}, .code = 0});
    EXPECT_EQ(native.code, unknownStatus);
}
