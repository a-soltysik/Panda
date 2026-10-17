#pragma once

// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <gmock/gmock.h>

#include <cstdint>
#include <memory>
#include <panda/Error.hpp>

#include "TextureResource.hpp"

namespace panda::test
{
class TextureResourceMock
{
public:
    using TextureResource = panda::detail::TextureResource;

    // gMock cannot generate trailing-return declarations.
    // NOLINTBEGIN(modernize-use-trailing-return-type)
    MOCK_METHOD(panda::Result<void>,
                create,
                (const vk::raii::Device&, panda::core::vulkan::resources::MemoryPool&, const panda::TextureDesc&),
                ());
    // cppcheck cannot parse this gMock declaration with a resource reference.
    // cppcheck-suppress syntaxError
    MOCK_METHOD(vk::Extent2D, getExtent, (const TextureResource&), (const));
    MOCK_METHOD(vk::Format, getFormat, (const panda::detail::TextureResource&), (const));
    MOCK_METHOD(vk::ImageUsageFlags, getUsage, (const panda::detail::TextureResource&), (const));
    MOCK_METHOD(vk::Image, getNative, (const panda::detail::TextureResource&), (const));
    MOCK_METHOD(bool,
                belongsTo,
                (const panda::detail::TextureResource&, const panda::core::vulkan::resources::MemoryPool&),
                (const));
    MOCK_METHOD(panda::Result<void>,
                reserveUpload,
                (const panda::detail::TextureResource&, panda::core::vulkan::upload::UploadBatchId),
                ());
    MOCK_METHOD(void,
                cancelUpload,
                (const panda::detail::TextureResource&, panda::core::vulkan::upload::UploadBatchId),
                ());
    MOCK_METHOD(void,
                commitUpload,
                (const panda::detail::TextureResource&, panda::core::vulkan::upload::UploadBatchId),
                ());
    // NOLINTEND(modernize-use-trailing-return-type)
};
}
