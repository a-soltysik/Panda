#pragma once

#include <gmock/gmock.h>

#include <expected>
#include <panda/WindowSurface.hpp>
#include <string>
#include <vector>

namespace panda::test
{
class WindowSurfaceMock : public WindowSurface
{
public:
    using ExtensionResult = std::expected<std::vector<std::string>, Error>;
    using SurfaceResult = std::expected<VkSurfaceKHR, Error>;
    using ExtentResult = std::expected<FramebufferExtent, Error>;

    // gMock cannot generate trailing-return declarations.
    // NOLINTBEGIN(modernize-use-trailing-return-type)
    MOCK_METHOD(ExtensionResult, getRequiredInstanceExtensions, (), (const, override));

    MOCK_METHOD(SurfaceResult, createSurface, (VkInstance), (const, override));

    MOCK_METHOD(ExtentResult, getFramebufferExtent, (), (const, override));
    // NOLINTEND(modernize-use-trailing-return-type)
};
}
