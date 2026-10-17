#pragma once

// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <gmock/gmock.h>

#include <expected>

#include "presentation/SwapchainGeneration.hpp"

namespace panda::test
{
class SwapchainGenerationMock
{
public:
    // gMock cannot generate trailing-return declarations.
    // NOLINTBEGIN(modernize-use-trailing-return-type)
    MOCK_METHOD((std::expected<panda::core::vulkan::SwapchainGeneration, Error>),
                createSwapchainGeneration,
                (const panda::core::vulkan::SwapchainGenerationRequest&),
                ());

    MOCK_METHOD(vk::ClearColorValue, encodedClear, (vk::Format), ());
    // NOLINTEND(modernize-use-trailing-return-type)
};
}
