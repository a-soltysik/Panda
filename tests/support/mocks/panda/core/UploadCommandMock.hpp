#pragma once

// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <gmock/gmock.h>

#include <cstdint>
#include <memory>
#include <panda/Error.hpp>
#include <span>

#include "upload/UploadCommand.hpp"

namespace panda::test
{
class UploadCommandMock
{
public:
    using OperationSpan = std::span<const panda::core::vulkan::upload::UploadOperation>;
    using Buffer = std::shared_ptr<panda::detail::BufferResource>;
    using BufferSpan = std::span<const Buffer>;

    // gMock cannot generate trailing-return declarations.
    // NOLINTBEGIN(modernize-use-trailing-return-type)
    MOCK_METHOD(panda::Result<void>, create, (const vk::raii::Device&, std::uint32_t, OperationSpan, BufferSpan), ());
    // cppcheck-suppress syntaxError
    MOCK_METHOD(vk::CommandBuffer, getCommand, (const panda::core::vulkan::upload::UploadCommand&), (const));
    MOCK_METHOD(std::uint64_t, getStagingSize, (const panda::core::vulkan::upload::UploadCommand&), (const));
    // NOLINTEND(modernize-use-trailing-return-type)
};
}
