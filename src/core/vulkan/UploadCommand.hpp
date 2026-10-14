#pragma once

// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <cstdint>
#include <memory>
#include <panda/Error.hpp>
#include <span>
#include <vector>

#include "UploadBatchImpl.hpp"

namespace panda::detail
{
// The command pool is destroyed before the referenced buffers. The manager only
// releases this packet after the submission clock confirms completion.
class UploadCommand final
{
public:
    [[nodiscard]] static auto create(const vk::raii::Device& device,
                                     std::uint32_t family,
                                     std::span<const BufferUpload> writes) -> Result<std::unique_ptr<UploadCommand>>;
    [[nodiscard]] auto getCommand() const -> vk::CommandBuffer;
    [[nodiscard]] auto getStagingSize() const -> std::uint64_t;

private:
    explicit UploadCommand(std::span<const BufferUpload> writes);
    [[nodiscard]] auto initialize(const vk::raii::Device& device, std::uint32_t family) -> Result<void>;
    [[nodiscard]] auto record() const -> Result<void>;

    std::vector<BufferUpload> _writes;
    vk::raii::CommandPool _pool {nullptr};
    vk::CommandBuffer _command;
};
}
