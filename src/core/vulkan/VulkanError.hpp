#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <panda/Assert.hpp>
#include <panda/Error.hpp>
#include <source_location>
#include <tuple>
#include <utility>
#include <vector>

#include "VulkanHpp.hpp"

namespace panda::detail
{

[[nodiscard]] auto makeVulkanError(vk::Result result, std::source_location source = std::source_location::current())
    -> Error;

// Hpp's nonthrowing RAII factories wrap the output even on failure. Vulkan does
// not guarantee that failed output is a valid owned object; detach it before
// the wrapper can destroy it. Keep this conversion at the adapter boundary.
template <typename Owner>
[[nodiscard]] auto checkedCreation(vk::ResultValue<Owner> created,
                                   std::source_location source = std::source_location::current())
    -> std::expected<Owner, Error>
{
    if (created.result != vk::Result::eSuccess)
    {
        std::ignore = created.value.release();
        return std::unexpected {makeVulkanError(created.result, source)};
    }
    return std::move(created.value);
}

// Enhanced Hpp enumerators inspect output counts even after a failed call.
// Failed outputs are undefined; inspect the status before using any output.
template <typename Element, typename Query>
[[nodiscard]] auto enumerateVulkan(Query query, std::source_location source = std::source_location::current())
    -> std::expected<std::vector<Element>, Error>
{
    auto values = std::vector<Element> {};
    auto result = vk::Result::eIncomplete;
    while (result == vk::Result::eIncomplete)
    {
        auto count = std::uint32_t {};
        const auto counted = std::invoke(query, &count, nullptr);
        if (counted != vk::Result::eSuccess)
        {
            return std::unexpected {makeVulkanError(counted, source)};
        }
        values.resize(count);
        if (count == 0)
        {
            return values;
        }
        result = std::invoke(query, &count, values.data());
        if (result != vk::Result::eSuccess && result != vk::Result::eIncomplete)
        {
            return std::unexpected {makeVulkanError(result, source)};
        }
        if (result == vk::Result::eSuccess)
        {
            expect(count <= values.size(), "Vulkan returned more entries than the supplied capacity");
            values.resize(count);
        }
    }
    return values;
}

}
