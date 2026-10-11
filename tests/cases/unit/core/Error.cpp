#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <panda/Error.hpp>
#include <source_location>
#include <string>
#include <utility>
#include <vulkan/vulkan.hpp>

namespace
{
auto makeErrorAtHelper() -> panda::Error
{
    return panda::makeError(panda::ErrorCode::InvalidArgument, "invalid request");
}
}

TEST(Error, OwnsMessageAndNativeStatusAndPreservesExplicitSource)
{
    const auto source = std::source_location::current();
    const auto error = panda::makeError(panda::ErrorCode::BackendFailure,
                                        "backend unavailable",
                                        panda::NativeError {.api = "test API", .code = -17},
                                        source);

    EXPECT_EQ(error.code, panda::ErrorCode::BackendFailure);
    EXPECT_EQ(error.message, "backend unavailable");
    ASSERT_TRUE(error.native.has_value());
    const auto native = error.native.value_or(panda::NativeError {.api = {}, .code = 0});
    EXPECT_EQ(native.api, "test API");
    EXPECT_EQ(native.code, -17);
    EXPECT_EQ(error.source.file_name(), source.file_name());
    EXPECT_EQ(error.source.line(), source.line());
}

TEST(Error, DefaultSourceLocationPointsToTheCallingFunction)
{
    const auto error = makeErrorAtHelper();
    EXPECT_THAT(error.source.function_name(), testing::HasSubstr("makeErrorAtHelper"));
}

TEST(Error, VulkanStatusesMapToCallerRecoveryActionsAndRetainNativeCodes)
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

TEST(Error, DefaultsUnrecognizedVulkanStatusToBackendFailure)
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
