// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
// clang-format on

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <panda/Buffer.hpp>
#include <panda/CompletionPoint.hpp>
#include <panda/Error.hpp>
#include <panda/Texture.hpp>
#include <panda/UploadBatch.hpp>
#include <span>
#include <utility>

#include "UploadBatchImpl.hpp"
#include "resources/BufferResource.hpp"
#include "resources/TextureResource.hpp"

namespace panda
{
UploadBatch::UploadBatch() = default;

UploadBatch::UploadBatch(std::unique_ptr<detail::UploadBatchImpl> impl)
    : _impl {std::move(impl)}
{
}

UploadBatch::UploadBatch(UploadBatch&& other) noexcept = default;
auto UploadBatch::operator=(UploadBatch&& other) noexcept -> UploadBatch& = default;
UploadBatch::~UploadBatch() = default;

auto UploadBatch::isValid() const noexcept -> bool
{
    return _impl != nullptr;
}

auto UploadBatch::write(const Buffer& destination, std::uint64_t offset, std::span<const std::byte> bytes)
    -> Result<void>
{
    if (!_impl)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Upload batch is empty or already submitted")};
    }
    return _impl->write(detail::BufferResource::getResource(destination), offset, bytes);
}

auto UploadBatch::write(const Texture& destination, std::span<const std::byte> bytes, std::uint64_t rowPitchBytes)
    -> Result<void>
{
    const auto extent = destination.getExtent();
    return write(destination, bytes, TextureUploadRegion {.extent = extent, .rowPitchBytes = rowPitchBytes});
}

auto UploadBatch::write(const Texture& destination, std::span<const std::byte> bytes, const TextureUploadRegion& region)
    -> Result<void>
{
    if (!_impl)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Upload batch is empty or already submitted")};
    }
    return _impl->write(detail::TextureResource::getResource(destination), bytes, region);
}

auto UploadBatch::submit() -> Result<CompletionPoint>
{
    if (!_impl)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Upload batch is empty or already submitted")};
    }
    auto submitted = _impl->submit();
    if (submitted)
    {
        _impl.reset();
    }
    return submitted;
}
}
