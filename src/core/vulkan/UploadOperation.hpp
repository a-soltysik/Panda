#pragma once

#include <cstdint>
#include <memory>
#include <panda/UploadBatch.hpp>
#include <variant>

namespace panda::detail
{
class BufferResource;
class TextureResource;

struct BufferUpload
{
    std::shared_ptr<BufferResource> staging;
    std::uint64_t stagingOffset;
    std::shared_ptr<BufferResource> destination;
    std::uint64_t offset;
    std::uint64_t size;
};

struct TextureUpload
{
    std::shared_ptr<BufferResource> staging;
    std::uint64_t stagingOffset;
    std::shared_ptr<TextureResource> destination;
    TextureUploadRegion region;
    std::uint32_t bufferRowLength;
    bool firstForTexture;
};

struct StagingSlice
{
    std::shared_ptr<BufferResource> buffer;
    std::uint64_t offset;
};

using UploadOperation = std::variant<BufferUpload, TextureUpload>;
}
