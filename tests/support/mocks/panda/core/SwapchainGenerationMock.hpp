#pragma once

// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <gmock/gmock.h>

#include <expected>

#include "SwapchainGeneration.hpp"

namespace panda::test
{
class SwapchainGenerationMock
{
public:
    // gMock cannot generate trailing-return declarations.
    // NOLINTBEGIN(modernize-use-trailing-return-type)
    MOCK_METHOD((std::expected<detail::SwapchainGeneration, Error>),
                createSwapchainGeneration,
                (const detail::SwapchainGenerationRequest&),
                ());

    MOCK_METHOD(vk::ClearColorValue, encodedClear, (vk::Format), ());
    // NOLINTEND(modernize-use-trailing-return-type)
};
}
