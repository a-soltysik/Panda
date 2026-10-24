// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include "external/ExternalContext.hpp"

#include <algorithm>
#include <cstdint>
#include <expected>
#include <optional>
#include <panda/Buffer.hpp>
#include <panda/Error.hpp>
#include <panda/ExternalBuffer.hpp>
#include <string_view>
#include <utility>
#include <vector>

#include "VulkanError.hpp"
#include "resources/BufferResource.hpp"
#include "resources/memory/MemoryPool.hpp"

namespace panda::detail
{
namespace
{
auto hasExtension(const std::vector<vk::ExtensionProperties>& extensions, std::string_view name) -> bool
{
    return std::ranges::contains(extensions, name, [](const auto& extension) {
        return std::string_view {extension.extensionName.data()};
    });
}

auto supportsMemoryExtension(const vk::raii::PhysicalDevice& physicalDevice, std::string_view name) -> Result<bool>
{
    const auto native = *physicalDevice;
    const auto listed = core::vulkan::detail::enumerateVulkan<vk::ExtensionProperties>(
        [&](std::uint32_t* count, vk::ExtensionProperties* output) {
            return native.enumerateDeviceExtensionProperties(nullptr, count, output);
        });
    if (!listed)
    {
        return std::unexpected {listed.error()};
    }
    return hasExtension(*listed, name);
}

auto unavailableExternalSupport(bool required) -> Result<std::optional<ExternalSupport>>
{
    if (required)
    {
        return std::unexpected {
            makeError(ErrorCode::Unsupported, "The selected Vulkan device cannot export external memory handles")};
    }
    return std::nullopt;
}

auto queryExternalSupport(const vk::raii::PhysicalDevice& physicalDevice, bool required, ExternalSupport support)
    -> Result<std::optional<ExternalSupport>>
{
    const auto supported = supportsMemoryExtension(physicalDevice, support.deviceExtensions.front());
    if (!supported)
    {
        return std::unexpected {supported.error()};
    }
    if (!*supported)
    {
        return unavailableExternalSupport(required);
    }
    return std::optional {std::move(support)};
}
}

auto ExternalContext::querySupport(const vk::raii::PhysicalDevice& physicalDevice, bool required)
    -> Result<std::optional<ExternalSupport>>
{
#ifdef _WIN32
    return queryExternalSupport(physicalDevice,
                                required,
                                ExternalSupport {.deviceExtensions = {"VK_KHR_external_memory_win32"},
                                                 .memoryHandleType = vk::ExternalMemoryHandleTypeFlagBits::eOpaqueWin32,
                                                 .publicHandleType = ExternalMemoryHandleType::OpaqueWin32});
#elifdef __linux__
    return queryExternalSupport(physicalDevice,
                                required,
                                ExternalSupport {.deviceExtensions = {"VK_KHR_external_memory_fd"},
                                                 .memoryHandleType = vk::ExternalMemoryHandleTypeFlagBits::eOpaqueFd,
                                                 .publicHandleType = ExternalMemoryHandleType::OpaqueFd});
#else
    std::ignore = physicalDevice;
    return unavailableExternalSupport(required);
#endif
}

ExternalContext::ExternalContext(const vk::raii::PhysicalDevice& physicalDevice,
                                 const vk::raii::Device& device,
                                 core::vulkan::resources::MemoryPool& pool,
                                 ExternalSupport support)
    : _physicalDevice {physicalDevice},
      _device {device},
      _pool {pool},
      _support {std::move(support)}
{
    const auto properties =
        _physicalDevice.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceIDProperties>();
    const auto& uuid = properties.get<vk::PhysicalDeviceIDProperties>().deviceUUID;
    std::ranges::copy(uuid, _deviceUuid.begin());
}

auto ExternalContext::createBuffer(const BufferDesc& description) -> Result<ExternalBuffer>
{
    auto resource = BufferResource::createExportable(_device, _pool, description, _support.memoryHandleType);
    if (!resource)
    {
        return std::unexpected {std::move(resource.error())};
    }
    const auto allocationSize = (*resource)->getReservedSize();
    const auto handle = exportMemoryHandle((*resource)->getNativeMemory());
    if (!handle)
    {
        return std::unexpected {handle.error()};
    }
    return ExternalBuffer {
        Buffer {std::move(*resource)},
        ExternalMemoryHandle {*handle, _support.publicHandleType},
        allocationSize,
        _deviceUuid
    };
}

auto ExternalContext::exportMemoryHandle(vk::DeviceMemory memory) const -> Result<std::intptr_t>
{
#ifdef _WIN32
    const auto info = vk::MemoryGetWin32HandleInfoKHR {.memory = memory, .handleType = _support.memoryHandleType};
    const auto exported = _device.getMemoryWin32HandleKHR(info);
#elifdef __linux__
    const auto info = vk::MemoryGetFdInfoKHR {.memory = memory, .handleType = _support.memoryHandleType};
    const auto exported = _device.getMemoryFdKHR(info);
#else
    std::ignore = memory;
    return std::unexpected {makeError(ErrorCode::Unsupported, "External memory handles are unsupported on this OS")};
#endif
#ifdef _WIN32
    if (exported.result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(exported.result)};
    }
    // Vulkan returns a HANDLE here; Panda carries opaque native handles as intptr_t.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast, performance-no-int-to-ptr)
    return reinterpret_cast<std::intptr_t>(exported.value);
#elifdef __linux__
    return static_cast<std::intptr_t>(exported.value);
#endif
}
}
