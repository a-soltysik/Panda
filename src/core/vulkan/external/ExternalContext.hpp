#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <panda/Buffer.hpp>
#include <panda/Error.hpp>
#include <panda/ExternalBuffer.hpp>
#include <string>
#include <vector>

#include "VulkanHpp.hpp"
#include "resources/memory/MemoryPool.hpp"

namespace panda::detail
{
struct ExternalSupport
{
    std::vector<std::string> deviceExtensions;
    vk::ExternalMemoryHandleTypeFlagBits memoryHandleType;
    ExternalMemoryHandleType publicHandleType;
};

class ExternalContext final
{
public:
    [[nodiscard]] static auto querySupport(const vk::raii::PhysicalDevice& physicalDevice, bool required)
        -> Result<std::optional<ExternalSupport>>;

    ExternalContext(const vk::raii::PhysicalDevice& physicalDevice,
                    const vk::raii::Device& device,
                    core::vulkan::resources::MemoryPool& pool,
                    ExternalSupport support);
    ~ExternalContext() = default;
    ExternalContext(const ExternalContext&) = delete;
    auto operator=(const ExternalContext&) -> ExternalContext& = delete;
    ExternalContext(ExternalContext&&) = delete;
    auto operator=(ExternalContext&&) -> ExternalContext& = delete;

    [[nodiscard]] auto createBuffer(const BufferDesc& description) -> Result<ExternalBuffer>;

private:
    [[nodiscard]] auto exportMemoryHandle(vk::DeviceMemory memory) const -> Result<std::intptr_t>;

    const vk::raii::PhysicalDevice& _physicalDevice;
    const vk::raii::Device& _device;
    core::vulkan::resources::MemoryPool& _pool;
    ExternalSupport _support;
    VulkanDeviceUuid _deviceUuid {};
};
}
