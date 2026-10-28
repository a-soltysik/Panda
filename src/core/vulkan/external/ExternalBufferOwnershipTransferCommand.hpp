#pragma once

// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <cstdint>
#include <memory>
#include <panda/Buffer.hpp>
#include <panda/Error.hpp>

namespace panda::detail
{
enum class ExternalBufferOwnershipTransferDirection : std::uint8_t
{
    Release,
    Acquire
};

class ExternalBufferOwnershipTransferCommand final
{
public:
    [[nodiscard]] static auto create(const vk::raii::Device& device,
                                     Buffer bufferOwner,
                                     std::uint32_t family,
                                     ExternalBufferOwnershipTransferDirection direction)
        -> Result<std::unique_ptr<ExternalBufferOwnershipTransferCommand>>;
    [[nodiscard]] auto getCommand() const -> vk::CommandBuffer;

private:
    ExternalBufferOwnershipTransferCommand(Buffer bufferOwner, vk::BufferMemoryBarrier2 barrier);
    [[nodiscard]] auto initialize(const vk::raii::Device& device) -> Result<void>;
    [[nodiscard]] auto record() const -> Result<void>;

    Buffer _bufferOwner;
    vk::BufferMemoryBarrier2 _barrier;
    vk::raii::CommandPool _pool {nullptr};
    vk::CommandBuffer _command;
};
}
