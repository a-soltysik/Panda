#pragma once

// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <panda/Buffer.hpp>
#include <panda/Context.hpp>
#include <panda/Error.hpp>

namespace panda::test
{
class VulkanBufferReadback final
{
public:
    [[nodiscard]] static auto create(const Context& context, const Buffer& source)
        -> Result<std::unique_ptr<VulkanBufferReadback>>;
    ~VulkanBufferReadback();
    VulkanBufferReadback(const VulkanBufferReadback&) = delete;
    auto operator=(const VulkanBufferReadback&) -> VulkanBufferReadback& = delete;
    VulkanBufferReadback(VulkanBufferReadback&&) = delete;
    auto operator=(VulkanBufferReadback&&) -> VulkanBufferReadback& = delete;
    [[nodiscard]] auto submitCopy() -> Result<void>;
    [[nodiscard]] auto read() -> Result<std::array<std::byte, 8>>;

private:
    VulkanBufferReadback(Buffer source, Buffer destination);
    [[nodiscard]] auto initialize(std::uint32_t family) -> Result<void>;
    [[nodiscard]] auto record(vk::CommandBuffer command) const -> Result<void>;
    void recordCopy(vk::CommandBuffer command) const;
    [[nodiscard]] auto wait() const -> Result<void>;

    Buffer _source;
    Buffer _destination;
    vk::Device _device;
    vk::Queue _queue;
    vk::CommandPool _pool;
    std::array<vk::CommandBuffer, 2> _commands {};
    std::array<vk::Fence, 2> _fences {};
    std::size_t _submitted {0};
};
}
