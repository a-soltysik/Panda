#pragma once

#include <cstdint>
#include <memory>
#include <panda/UploadBatch.hpp>
#include <variant>

namespace panda::detail
{
class BufferResource;
class TextureResource;
}

namespace panda::core::vulkan::upload
{
namespace api_detail = ::panda::detail;

struct BufferUpload
{
    std::shared_ptr<api_detail::BufferResource> staging;
    std::uint64_t stagingOffset;
    std::shared_ptr<api_detail::BufferResource> destination;
    std::uint64_t offset;
    std::uint64_t size;
};

struct TextureUpload
{
    std::shared_ptr<api_detail::BufferResource> staging;
    std::uint64_t stagingOffset;
    std::shared_ptr<api_detail::TextureResource> destination;
    TextureUploadRegion region;
    std::uint32_t bufferRowLength;
    bool firstForTexture;
};

struct StagingSlice
{
    std::shared_ptr<api_detail::BufferResource> buffer;
    std::uint64_t offset;
};

using UploadOperation = std::variant<BufferUpload, TextureUpload>;
}
