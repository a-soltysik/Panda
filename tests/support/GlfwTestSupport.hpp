#pragma once

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <panda/Error.hpp>

namespace panda::test
{
[[nodiscard]] inline auto isGlfwPlatformUnavailable(const panda::Error& error) -> bool
{
    const auto native = error.native.value_or(panda::NativeError {});
    return native.api == "GLFW" && (native.code == GLFW_PLATFORM_UNAVAILABLE || native.code == GLFW_PLATFORM_ERROR);
}
}
