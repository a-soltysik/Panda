#pragma once

#include <panda/Assert.hpp>

// Vulkan-Hpp checks both internal invariants and VkResult values with assertions.
// Preserve internal checks in debug builds; callers inspect VkResult, including
// recoverable WSI statuses. Its release dispatcher omits assertion helpers.
#ifndef NDEBUG
#    define VULKAN_HPP_ASSERT(condition) ::panda::expect((condition), #condition)
#else
#    define VULKAN_HPP_ASSERT(condition) static_cast<void>(0)
#endif
#define VULKAN_HPP_ASSERT_ON_RESULT(condition) static_cast<void>(condition)  // NOLINT(cppcoreguidelines-macro-usage)

#include <functional>
#include <utility>
#include <vulkan/vulkan.hpp>            // IWYU pragma: export
#include <vulkan/vulkan_to_string.hpp>  // IWYU pragma: export

namespace panda::detail
{

[[nodiscard]] inline auto parentlessDeleter()
    -> vk::detail::ObjectDestroy<vk::detail::NoParent, VULKAN_HPP_DEFAULT_DISPATCHER_TYPE>
{
    return {nullptr, VULKAN_HPP_DEFAULT_DISPATCHER};
}

[[nodiscard]] inline auto deviceDeleter(vk::Device device)
    -> vk::detail::ObjectDestroy<vk::Device, VULKAN_HPP_DEFAULT_DISPATCHER_TYPE>
{
    return {device};
}

[[nodiscard]] inline auto instanceDeleter(vk::Instance instance)
    -> vk::detail::ObjectDestroy<vk::Instance, VULKAN_HPP_DEFAULT_DISPATCHER_TYPE>
{
    return {instance};
}

// Enhanced createUnique wrappers can wrap an uninitialized output handle when
// Vulkan returns an error. Initialize the output and adopt it only on success.
template <typename Handle, typename Creator, typename Deleter>
[[nodiscard]] auto createOwned(Creator&& create, Deleter&& deleter)
    -> vk::ResultValue<vk::UniqueHandle<Handle, VULKAN_HPP_DEFAULT_DISPATCHER_TYPE>>
{
    auto handle = Handle {};
    const auto result = std::invoke(std::forward<Creator>(create), &handle);
    using Owned = vk::UniqueHandle<Handle, VULKAN_HPP_DEFAULT_DISPATCHER_TYPE>;
    if (result != vk::Result::eSuccess)
    {
        return {result, Owned {}};
    }
    return {
        result,
        Owned {handle, std::forward<Deleter>(deleter)}
    };
}

}
