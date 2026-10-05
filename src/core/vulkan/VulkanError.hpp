#pragma once

#include <panda/Error.hpp>
#include <source_location>

#include "VulkanHpp.hpp"

namespace panda::detail
{

[[nodiscard]] auto makeVulkanError(vk::Result result, std::source_location source = std::source_location::current())
    -> Error;

}
