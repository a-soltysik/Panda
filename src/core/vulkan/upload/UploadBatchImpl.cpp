// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
// clang-format on

#include "upload/UploadBatchImpl.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <glm/ext/vector_uint2.hpp>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <panda/base/Error.hpp>
#include <panda/resources/Texture.hpp>
#include <panda/submission/CompletionPoint.hpp>
#include <panda/submission/UploadBatch.hpp>
#include <span>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

#include "../../EnvironmentMapFilter.hpp"
#include "resources/BufferResource.hpp"
#include "resources/MeshResource.hpp"
#include "resources/TextureResource.hpp"
#include "upload/TextureMipGeneration.hpp"
#include "upload/UploadBatchId.hpp"
#include "upload/UploadManager.hpp"
#include "upload/UploadOperation.hpp"

namespace panda::detail
{
using batch_upload::BufferUpload;
using batch_upload::StagingSlice;
using batch_upload::TextureUpload;
using batch_upload::UploadBatchId;
using batch_upload::UploadManager;
using batch_upload::UploadOperation;

namespace
{
constexpr auto stagingPageSize = std::uint64_t {4} * 1024;

class StagingRollback final
{
public:
    StagingRollback(std::vector<std::shared_ptr<BufferResource>>& pages, std::uint64_t& offset)
        : _pages {pages},
          _offset {offset},
          _pageCount {pages.size()},
          _savedOffset {offset}
    {
    }

    StagingRollback(const StagingRollback&) = delete;
    auto operator=(const StagingRollback&) -> StagingRollback& = delete;
    StagingRollback(StagingRollback&&) = delete;
    auto operator=(StagingRollback&&) -> StagingRollback& = delete;

    ~StagingRollback() noexcept
    {
        if (!_committed)
        {
            while (_pages.size() > _pageCount)
            {
                _pages.pop_back();
            }
            _offset = _savedOffset;
        }
    }

    void commit() noexcept
    {
        _committed = true;
    }

private:
    std::vector<std::shared_ptr<BufferResource>>& _pages;
    std::uint64_t& _offset;
    std::size_t _pageCount;
    std::uint64_t _savedOffset;
    bool _committed {false};
};

auto alignOffset(std::uint64_t offset, std::uint64_t alignment) -> std::optional<std::uint64_t>
{
    const auto remainder = offset % alignment;
    const auto padding = remainder == 0 ? 0 : alignment - remainder;
    if (padding > std::numeric_limits<std::uint64_t>::max() - offset)
    {
        return std::nullopt;
    }
    return offset + padding;
}

auto fitsPageRange(std::uint64_t offset, std::size_t size, std::uint64_t pageSize) -> bool
{
    return offset <= pageSize && size <= pageSize - offset;
}

auto validateStageRequest(std::span<const std::byte> bytes, std::uint64_t alignment) -> Result<void>
{
    if (bytes.empty() || alignment == 0)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Staging writes must be nonempty and aligned")};
    }
    return {};
}

auto allocateEnvironmentFacePixels(std::uint32_t width, std::uint32_t height) -> Result<std::vector<std::uint16_t>>
{
    if (height == 0 || width > std::numeric_limits<std::size_t>::max() / height / 4U)
    {
        return std::unexpected {makeError(ErrorCode::ResourceExhausted, "Environment face is too large")};
    }
    auto pixels = std::vector<std::uint16_t> {};
    const auto pixelCount = static_cast<std::size_t>(width) * height * 4U;
    if (pixelCount > pixels.max_size())
    {
        return std::unexpected {makeError(ErrorCode::ResourceExhausted, "Environment face is too large")};
    }
    pixels.resize(pixelCount);
    return pixels;
}

void copyEnvironmentFaceRows(std::span<const std::byte> source,
                             std::span<std::uint16_t> destination,
                             glm::uvec2 extent,
                             std::uint64_t sourceRowPitch)
{
    const auto rowBytes = static_cast<std::size_t>(extent.x) * 4U * sizeof(std::uint16_t);
    const auto rowPitch = sourceRowPitch == 0 ? rowBytes : sourceRowPitch;
    const auto destinationBytes = std::as_writable_bytes(destination);
    for (auto row = std::uint32_t {}; row < extent.y; ++row)
    {
        const auto sourceOffset = static_cast<std::size_t>(row) * rowPitch;
        const auto destinationOffset = static_cast<std::size_t>(row) * rowBytes;
        const auto byteCount = rowBytes;
        std::ranges::copy(source.subspan(sourceOffset, byteCount),
                          destinationBytes.subspan(destinationOffset, byteCount).begin());
    }
}

auto makeEnvironmentFacePixels(std::span<const std::byte> bytes,
                               std::uint32_t width,
                               std::uint32_t height,
                               std::uint64_t sourceRowPitch) -> Result<std::vector<std::uint16_t>>
{
    auto pixels = allocateEnvironmentFacePixels(width, height);
    if (!pixels)
    {
        return std::unexpected {pixels.error()};
    }
    copyEnvironmentFaceRows(bytes, *pixels, {width, height}, sourceRowPitch);
    if (const auto valid = validateEnvironmentFace(*pixels, width); !valid)
    {
        return std::unexpected {valid.error()};
    }
    return std::move(*pixels);
}

void releaseUploadReservation(const UploadOperation& operation, UploadBatchId batchId)
{
    const auto* upload = std::get_if<TextureUpload>(&operation);
    if (upload != nullptr && upload->firstForTexture)
    {
        upload->destination->cancelUpload(batchId);
    }
}

void commitUploadReservation(const UploadOperation& operation, UploadBatchId batchId)
{
    const auto* upload = std::get_if<TextureUpload>(&operation);
    if (upload != nullptr && upload->firstForTexture)
    {
        upload->destination->commitUpload(batchId);
    }
}

void commitUploadReservations(std::span<const UploadOperation> operations, UploadBatchId batchId)
{
    for (const auto& operation : operations)
    {
        commitUploadReservation(operation, batchId);
    }
}

auto refersTo([[maybe_unused]] const BufferUpload& upload,
              [[maybe_unused]] const std::shared_ptr<TextureResource>& texture) -> bool
{
    return false;
}

auto refersTo(const TextureUpload& upload, const std::shared_ptr<TextureResource>& texture) -> bool
{
    return upload.destination == texture;
}

auto reserveTexture(const std::shared_ptr<TextureResource>& texture, UploadBatchId batchId, bool alreadyInBatch)
    -> Result<bool>
{
    if (alreadyInBatch)
    {
        return false;
    }
    const auto reserved = texture->reserveUpload(batchId);
    if (!reserved)
    {
        return std::unexpected {reserved.error()};
    }
    return true;
}

auto validateMeshWriteSizes(const MeshResource& destination,
                            std::span<const std::byte> vertexBytes,
                            std::span<const std::byte> indexBytes) -> Result<void>
{
    const auto& layout = destination.getLayout();
    if (vertexBytes.size() != layout.getVertexSize() || indexBytes.size() != layout.getIndexSize())
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Mesh upload data sizes do not match its description")};
    }
    return {};
}

auto validateMeshWriteBuffers(const UploadManager& manager,
                              const MeshResource& destination,
                              std::span<const std::byte> vertexBytes,
                              std::span<const std::byte> indexBytes) -> Result<void>
{
    const auto validVertex = manager.validateWrite(destination.getVertexBuffer(), 0, vertexBytes.size());
    if (!validVertex)
    {
        return validVertex;
    }
    const auto& indexBuffer = destination.getIndexBuffer();
    if (indexBuffer)
    {
        const auto validIndices = manager.validateWrite(indexBuffer, 0, indexBytes.size());
        if (!validIndices)
        {
            return validIndices;
        }
    }
    return {};
}

auto validateMeshWrite(const UploadManager& manager,
                       const std::shared_ptr<MeshResource>& destination,
                       std::span<const std::byte> vertexBytes,
                       std::span<const std::byte> indexBytes) -> Result<void>
{
    if (!destination)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Mesh handle is empty")};
    }
    const auto validSizes = validateMeshWriteSizes(*destination, vertexBytes, indexBytes);
    if (!validSizes)
    {
        return std::unexpected {validSizes.error()};
    }
    return validateMeshWriteBuffers(manager, *destination, vertexBytes, indexBytes);
}

void cancelReservation(const std::shared_ptr<TextureResource>& destination, UploadBatchId batchId, bool reserved)
{
    if (reserved)
    {
        destination->cancelUpload(batchId);
    }
}
}

UploadBatchImpl::UploadBatchImpl(UploadManager& manager, UploadBatchId id)
    : _manager {manager},
      _id {id}
{
    _manager.retainBatch();
}

UploadBatchImpl::~UploadBatchImpl()
{
    for (const auto& operation : _writes)
    {
        releaseUploadReservation(operation, _id);
    }
    for (const auto& generatedWrite : _generatedEnvironmentWrites)
    {
        generatedWrite.destination->cancelUpload(_id);
    }
    _manager.releaseBatch();
}

auto UploadBatchImpl::stage(std::span<const std::byte> bytes, std::uint64_t alignment) -> Result<StagingSlice>
{
    const auto valid = validateStageRequest(bytes, alignment);
    if (!valid)
    {
        return std::unexpected {valid.error()};
    }
    if (const auto collected = _manager.collect(); !collected)
    {
        return std::unexpected {collected.error()};
    }
    auto current = stageInCurrentPage(bytes, alignment);
    if (!current)
    {
        return std::unexpected {current.error()};
    }
    if (*current)
    {
        return std::move(**current);
    }
    return createStagingPage(bytes);
}

auto UploadBatchImpl::stageInCurrentPage(std::span<const std::byte> bytes, std::uint64_t alignment)
    -> Result<std::optional<StagingSlice>>
{
    if (_stagingPages.empty())
    {
        return std::optional<StagingSlice> {};
    }
    const auto offset = alignOffset(_stagingOffset, alignment);
    if (!offset)
    {
        return std::optional<StagingSlice> {};
    }
    const auto& page = _stagingPages.back();
    if (!fitsPageRange(*offset, bytes.size(), page->getSize()))
    {
        return std::optional<StagingSlice> {};
    }
    const auto copied = page->writeMapped(*offset, bytes);
    if (!copied)
    {
        return std::unexpected {copied.error()};
    }
    _stagingOffset = *offset + bytes.size();
    return std::optional<StagingSlice> {
        StagingSlice {.buffer = page, .offset = *offset}
    };
}

auto UploadBatchImpl::createStagingPage(std::span<const std::byte> bytes) -> Result<StagingSlice>
{
    const auto pageSize = std::max<std::uint64_t>(stagingPageSize, bytes.size());
    auto page = _manager.createStaging(pageSize);
    if (!page)
    {
        return std::unexpected {page.error()};
    }
    const auto copied = (*page)->writeMapped(0, bytes);
    if (!copied)
    {
        return std::unexpected {copied.error()};
    }
    _stagingPages.push_back(*page);
    _stagingOffset = bytes.size();
    return StagingSlice {.buffer = std::move(*page), .offset = 0};
}

auto UploadBatchImpl::createBufferUpload(const std::shared_ptr<BufferResource>& destination,
                                         std::span<const std::byte> bytes) -> Result<BufferUpload>
{
    auto staging = stage(bytes, 1);
    if (!staging)
    {
        return std::unexpected {staging.error()};
    }
    return BufferUpload {.staging = std::move(staging->buffer),
                         .stagingOffset = staging->offset,
                         .destination = destination,
                         .offset = 0,
                         .size = bytes.size()};
}

auto UploadBatchImpl::createMeshUploads(const std::shared_ptr<MeshResource>& destination,
                                        std::span<const std::byte> vertexBytes,
                                        std::span<const std::byte> indexBytes) -> Result<std::vector<UploadOperation>>
{
    const auto valid = validateMeshWrite(_manager, destination, vertexBytes, indexBytes);
    if (!valid)
    {
        return std::unexpected {valid.error()};
    }
    auto writes = std::vector<UploadOperation> {};
    writes.reserve(destination->getIndexBuffer() ? 2 : 1);
    auto vertexUpload = createBufferUpload(destination->getVertexBuffer(), vertexBytes);
    if (!vertexUpload)
    {
        return std::unexpected {vertexUpload.error()};
    }
    writes.emplace_back(std::move(*vertexUpload));
    const auto& indexBuffer = destination->getIndexBuffer();
    if (indexBuffer)
    {
        auto indexUpload = createBufferUpload(indexBuffer, indexBytes);
        if (!indexUpload)
        {
            return std::unexpected {indexUpload.error()};
        }
        writes.emplace_back(std::move(*indexUpload));
    }
    return writes;
}

auto UploadBatchImpl::write(const std::shared_ptr<BufferResource>& destination,
                            std::uint64_t offset,
                            std::span<const std::byte> bytes) -> Result<void>
{
    const auto valid = _manager.validateWrite(destination, offset, bytes.size());
    if (!valid)
    {
        return valid;
    }
    auto upload = createBufferUpload(destination, bytes);
    if (!upload)
    {
        return std::unexpected {upload.error()};
    }
    upload->offset = offset;
    _writes.emplace_back(std::move(*upload));
    return {};
}

auto UploadBatchImpl::write(const std::shared_ptr<MeshResource>& destination,
                            std::span<const std::byte> vertexBytes,
                            std::span<const std::byte> indexBytes) -> Result<void>
{
    auto uploads = createMeshUploads(destination, vertexBytes, indexBytes);
    if (!uploads)
    {
        return std::unexpected {uploads.error()};
    }
    if (_writes.size() > _writes.max_size() - uploads->size())
    {
        return std::unexpected {makeError(ErrorCode::ResourceExhausted, "Upload batch cannot retain mesh writes")};
    }
    _writes.reserve(_writes.size() + uploads->size());
    std::ranges::move(*uploads, std::back_inserter(_writes));
    return {};
}

auto UploadBatchImpl::write(const std::shared_ptr<TextureResource>& destination,
                            std::span<const std::byte> bytes,
                            const TextureUploadRegion& region) -> Result<void>
{
    if (destination && destination->getMipPolicy() == TextureMipPolicy::GenerateSpecularEnvironment)
    {
        return appendGeneratedEnvironmentFace(destination, bytes, region);
    }
    auto request = prepareTextureMipWrite(destination, bytes, region);
    if (!request)
    {
        return std::unexpected {request.error()};
    }
    return request->destination->getMipPolicy() == TextureMipPolicy::Generate ? appendGeneratedTextureMips(*request)
                                                                              : appendTextureMip(*request);
}

auto UploadBatchImpl::prepareTextureMipWrite(const std::shared_ptr<TextureResource>& destination,
                                             std::span<const std::byte> bytes,
                                             const TextureUploadRegion& region) -> Result<TextureMipWriteRequest>
{
    const auto layout = _manager.validateTextureWrite(destination, region, bytes.size());
    if (!layout)
    {
        return std::unexpected {layout.error()};
    }
    const auto alreadyInBatch = hasTexture(destination);
    if (isTextureMipWriteDuplicate(destination, region, alreadyInBatch))
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Texture subresource was already written")};
    }
    const auto reserved = reserveTexture(destination, _id, alreadyInBatch);
    if (!reserved)
    {
        return std::unexpected {reserved.error()};
    }
    return TextureMipWriteRequest {.destination = destination,
                                   .bytes = bytes,
                                   .region = region,
                                   .rowLength = layout->rowLength,
                                   .bufferOffsetAlignment = layout->bufferOffsetAlignment,
                                   .reserved = *reserved};
}

auto UploadBatchImpl::isTextureMipWriteDuplicate(const std::shared_ptr<TextureResource>& destination,
                                                 const TextureUploadRegion& region,
                                                 bool alreadyInBatch) const -> bool
{
    const auto mipPolicy = destination->getMipPolicy();
    return (mipPolicy == TextureMipPolicy::Generate && alreadyInBatch) ||
           ((mipPolicy == TextureMipPolicy::Supplied || destination->getPublicType() == TextureType::CubeMap) &&
            hasTextureMip(destination, region.mipLevel, region.arrayLayer));
}

auto UploadBatchImpl::createGeneratedEnvironmentFace(const std::shared_ptr<TextureResource>& destination,
                                                     std::span<const std::byte> bytes,
                                                     const TextureUploadRegion& region)
    -> Result<std::vector<std::uint16_t>>
{
    if (!destination || destination->getPublicType() != TextureType::CubeMap ||
        destination->getMipPolicy() != TextureMipPolicy::GenerateSpecularEnvironment)
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Generated environment upload needs a cubemap texture")};
    }
    if (region.mipLevel != 0)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Generated environment uploads need mip zero")};
    }
    const auto extent = destination->getExtent();
    const auto layout = _manager.validateTextureWrite(destination, region, bytes.size());
    if (!layout)
    {
        return std::unexpected {layout.error()};
    }
    return makeEnvironmentFacePixels(bytes, extent.width, extent.height, region.rowPitchBytes);
}

auto UploadBatchImpl::appendGeneratedEnvironmentFace(const std::shared_ptr<TextureResource>& destination,
                                                     std::span<const std::byte> bytes,
                                                     const TextureUploadRegion& region) -> Result<void>
{
    auto face = createGeneratedEnvironmentFace(destination, bytes, region);
    if (!face)
    {
        return std::unexpected {face.error()};
    }
    const auto pending =
        std::ranges::find(_generatedEnvironmentWrites, destination, &GeneratedEnvironmentWrite::destination);
    if (pending != _generatedEnvironmentWrites.end() && pending->hasFace.at(region.arrayLayer))
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Cubemap face was already written")};
    }
    auto pendingWrite = getOrCreateGeneratedEnvironmentWrite(destination);
    if (!pendingWrite)
    {
        return std::unexpected {pendingWrite.error()};
    }
    auto& generatedWrite = **pendingWrite;
    if (std::ranges::count(generatedWrite.hasFace, true) == environmentMapFaceCount - 1U)
    {
        return appendGeneratedEnvironmentMips(destination, generatedWrite, region.arrayLayer, *face);
    }
    generatedWrite.faces.at(region.arrayLayer) = std::move(*face);
    generatedWrite.hasFace.at(region.arrayLayer) = true;
    return {};
}

auto UploadBatchImpl::getOrCreateGeneratedEnvironmentWrite(const std::shared_ptr<TextureResource>& destination)
    -> Result<GeneratedEnvironmentWrite*>
{
    const auto pending =
        std::ranges::find(_generatedEnvironmentWrites, destination, &GeneratedEnvironmentWrite::destination);
    if (pending != _generatedEnvironmentWrites.end())
    {
        return &*pending;
    }
    if (hasTexture(destination))
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Generated environment texture was already written in this batch")};
    }
    _generatedEnvironmentWrites.push_back({.destination = destination, .faces = {}, .hasFace = {}});
    const auto reserved = destination->reserveUpload(_id);
    if (!reserved)
    {
        _generatedEnvironmentWrites.pop_back();
        return std::unexpected {reserved.error()};
    }
    return &_generatedEnvironmentWrites.back();
}

auto UploadBatchImpl::createGeneratedEnvironmentMipChain(const std::shared_ptr<TextureResource>& destination,
                                                         const GeneratedEnvironmentWrite& write,
                                                         std::uint32_t finalFace,
                                                         std::span<const std::uint16_t> face)
    -> Result<SpecularEnvironmentMipChain>
{
    auto sourceFaces = std::array<std::span<const std::uint16_t>, environmentMapFaceCount> {};
    for (auto sourceFace = std::size_t {}; sourceFace < sourceFaces.size(); ++sourceFace)
    {
        sourceFaces.at(sourceFace) =
            sourceFace == finalFace ? face : std::span<const std::uint16_t> {write.faces.at(sourceFace)};
    }
    const auto extent = destination->getExtent();
    auto mipChain = prefilterSpecularEnvironmentMap({.faceSize = extent.width, .faces = sourceFaces},
                                                    destination->getSpecularEnvironmentSamplesPerTexel());
    if (!mipChain)
    {
        return std::unexpected {mipChain.error()};
    }
    if (mipChain->levels.size() != destination->getMipLevels())
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Generated cubemap mip count does not match")};
    }
    return std::move(*mipChain);
}

auto UploadBatchImpl::appendGeneratedEnvironmentMips(const std::shared_ptr<TextureResource>& destination,
                                                     const GeneratedEnvironmentWrite& write,
                                                     std::uint32_t finalFace,
                                                     std::span<const std::uint16_t> face) -> Result<void>
{
    auto stagingRollback = StagingRollback {_stagingPages, _stagingOffset};
    auto mipChain = createGeneratedEnvironmentMipChain(destination, write, finalFace, face);
    if (!mipChain)
    {
        return std::unexpected {mipChain.error()};
    }
    auto uploads = createGeneratedEnvironmentUploads(destination, *mipChain);
    if (!uploads)
    {
        return std::unexpected {uploads.error()};
    }
    if (_writes.size() > _writes.max_size() - uploads->size())
    {
        return std::unexpected {makeError(ErrorCode::ResourceExhausted, "Upload batch cannot retain cubemap writes")};
    }
    _writes.reserve(_writes.size() + uploads->size());
    std::ranges::move(*uploads, std::back_inserter(_writes));
    const auto pending =
        std::ranges::find(_generatedEnvironmentWrites, destination, &GeneratedEnvironmentWrite::destination);
    _generatedEnvironmentWrites.erase(pending);
    stagingRollback.commit();
    return {};
}

auto UploadBatchImpl::appendGeneratedTextureMips(const TextureMipWriteRequest& request) -> Result<void>
{
    const auto extent = request.destination->getExtent();
    const auto mipChain = batch_upload::generateTextureMipChain({.format = request.destination->getPublicFormat(),
                                                                 .width = extent.width,
                                                                 .height = extent.height,
                                                                 .baseLevel = request.bytes,
                                                                 .rowPitchBytes = request.region.rowPitchBytes});
    if (!mipChain)
    {
        cancelReservation(request.destination, _id, request.reserved);
        return std::unexpected {mipChain.error()};
    }
    auto uploads = createGeneratedMipUploads(request, *mipChain);
    if (!uploads)
    {
        cancelReservation(request.destination, _id, request.reserved);
        return std::unexpected {uploads.error()};
    }
    _writes.reserve(_writes.size() + uploads->size());
    std::ranges::move(*uploads, std::back_inserter(_writes));
    return {};
}

auto UploadBatchImpl::createEnvironmentFaceUpload(const std::shared_ptr<TextureResource>& destination,
                                                  const EnvironmentMapMip& mip,
                                                  std::uint32_t mipLevel,
                                                  std::uint32_t face) -> Result<UploadOperation>
{
    const auto pixels = std::span {mip.faces.at(face)};
    const auto bytes = std::as_bytes(pixels);
    const auto region = TextureUploadRegion {.extent = mip.extent, .mipLevel = mipLevel, .arrayLayer = face};
    const auto layout = _manager.validateTextureWrite(destination, region, bytes.size());
    if (!layout)
    {
        return std::unexpected {layout.error()};
    }
    const auto staging = stage(bytes, layout->bufferOffsetAlignment);
    if (!staging)
    {
        return std::unexpected {staging.error()};
    }
    return UploadOperation {
        TextureUpload {.staging = staging->buffer,
                       .stagingOffset = staging->offset,
                       .destination = destination,
                       .region = region,
                       .bufferRowLength = layout->rowLength,
                       .firstForTexture = mipLevel == 0 && face == 0}
    };
}

auto UploadBatchImpl::createGeneratedEnvironmentUploads(const std::shared_ptr<TextureResource>& destination,
                                                        const SpecularEnvironmentMipChain& mipChain)
    -> Result<std::vector<UploadOperation>>
{
    try
    {
        auto uploads = std::vector<UploadOperation> {};
        uploads.reserve(mipChain.levels.size() * environmentMapFaceCount);
        const auto appended = appendEnvironmentMipUploads(destination, mipChain, uploads);
        if (!appended)
        {
            return std::unexpected {appended.error()};
        }
        return uploads;
    }
    catch (const std::length_error&)
    {
        return std::unexpected {makeError(ErrorCode::ResourceExhausted, "Upload batch cannot retain cubemap writes")};
    }
}

auto UploadBatchImpl::appendEnvironmentMipUploads(const std::shared_ptr<TextureResource>& destination,
                                                  const SpecularEnvironmentMipChain& mipChain,
                                                  std::vector<UploadOperation>& uploads) -> Result<void>
{
    for (auto mipLevel = std::uint32_t {}; mipLevel < mipChain.levels.size(); ++mipLevel)
    {
        const auto appended =
            appendEnvironmentFaceUploads(destination, mipChain.levels.at(mipLevel), mipLevel, uploads);
        if (!appended)
        {
            return std::unexpected {appended.error()};
        }
    }
    return {};
}

auto UploadBatchImpl::appendEnvironmentFaceUploads(const std::shared_ptr<TextureResource>& destination,
                                                   const EnvironmentMapMip& mip,
                                                   std::uint32_t mipLevel,
                                                   std::vector<UploadOperation>& uploads) -> Result<void>
{
    for (auto face = std::uint32_t {}; face < mip.faces.size(); ++face)
    {
        auto upload = createEnvironmentFaceUpload(destination, mip, mipLevel, face);
        if (!upload)
        {
            return std::unexpected {upload.error()};
        }
        uploads.push_back(std::move(*upload));
    }
    return {};
}

auto UploadBatchImpl::createGeneratedMipUploads(const TextureMipWriteRequest& request,
                                                const std::vector<std::vector<std::byte>>& mipChain)
    -> Result<std::vector<UploadOperation>>
{
    const auto extent = request.destination->getExtent();
    auto uploads = std::vector<UploadOperation> {};
    uploads.reserve(mipChain.size());
    for (auto mipLevel = std::uint32_t {}; mipLevel < mipChain.size(); ++mipLevel)
    {
        const auto mipRegion = TextureUploadRegion {
            .extent = {std::max(1U, extent.width >> mipLevel), std::max(1U, extent.height >> mipLevel)},
            .mipLevel = mipLevel
        };
        const auto staging = stage(mipChain.at(mipLevel), request.bufferOffsetAlignment);
        if (!staging)
        {
            return std::unexpected {staging.error()};
        }
        uploads.emplace_back(TextureUpload {.staging = staging->buffer,
                                            .stagingOffset = staging->offset,
                                            .destination = request.destination,
                                            .region = mipRegion,
                                            .bufferRowLength = 0,
                                            .firstForTexture = request.reserved && mipLevel == 0});
    }
    return uploads;
}

auto UploadBatchImpl::appendTextureMip(const TextureMipWriteRequest& request) -> Result<void>
{
    auto staging = stage(request.bytes, request.bufferOffsetAlignment);
    if (!staging)
    {
        cancelReservation(request.destination, _id, request.reserved);
        return std::unexpected {staging.error()};
    }
    _writes.emplace_back(TextureUpload {.staging = std::move(staging->buffer),
                                        .stagingOffset = staging->offset,
                                        .destination = request.destination,
                                        .region = request.region,
                                        .bufferRowLength = request.rowLength,
                                        .firstForTexture = request.reserved});
    return {};
}

auto UploadBatchImpl::hasTexture(const std::shared_ptr<TextureResource>& texture) const -> bool
{
    return std::ranges::any_of(_generatedEnvironmentWrites,
                               [&](const GeneratedEnvironmentWrite& write) {
                                   return write.destination == texture;
                               }) ||
           std::ranges::any_of(_writes, [&](const UploadOperation& operation) {
               return std::visit(
                   [&](const auto& upload) {
                       return refersTo(upload, texture);
                   },
                   operation);
           });
}

auto UploadBatchImpl::hasTextureMip(const std::shared_ptr<TextureResource>& texture,
                                    std::uint32_t mipLevel,
                                    std::uint32_t arrayLayer) const -> bool
{
    return (mipLevel == 0 && arrayLayer < environmentMapFaceCount &&
            std::ranges::any_of(
                _generatedEnvironmentWrites,
                [&](const GeneratedEnvironmentWrite& write) {
                    return write.destination == texture && write.hasFace.at(arrayLayer);
                })) ||
           std::ranges::any_of(_writes, [&](const UploadOperation& operation) {
               const auto* upload = std::get_if<TextureUpload>(&operation);
               return upload != nullptr && upload->destination == texture && upload->region.mipLevel == mipLevel &&
                      upload->region.arrayLayer == arrayLayer;
           });
}

auto UploadBatchImpl::isFirstSuppliedMipUpload(std::size_t index, const TextureUpload& upload) const -> bool
{
    return std::ranges::none_of(std::span {_writes}.first(index), [&](const UploadOperation& operation) {
        const auto* previous = std::get_if<TextureUpload>(&operation);
        return previous != nullptr && previous->destination == upload.destination;
    });
}

auto UploadBatchImpl::hasCompleteSuppliedLayerMipChain(const std::shared_ptr<TextureResource>& texture,
                                                       std::uint32_t arrayLayer) const -> bool
{
    for (auto mipLevel = std::uint32_t {}; mipLevel < texture->getMipLevels(); ++mipLevel)
    {
        if (!hasTextureMip(texture, mipLevel, arrayLayer))
        {
            return false;
        }
    }
    return true;
}

auto UploadBatchImpl::hasCompleteSuppliedMipChain(const std::shared_ptr<TextureResource>& texture) const -> bool
{
    for (auto arrayLayer = std::uint32_t {}; arrayLayer < texture->getArrayLayers(); ++arrayLayer)
    {
        if (!hasCompleteSuppliedLayerMipChain(texture, arrayLayer))
        {
            return false;
        }
    }
    return true;
}

auto UploadBatchImpl::validateSuppliedMipChains() const -> Result<void>
{
    for (auto index = std::size_t {}; index < _writes.size(); ++index)
    {
        const auto* first = std::get_if<TextureUpload>(&_writes.at(index));
        if (first == nullptr ||
            (first->destination->getMipPolicy() != TextureMipPolicy::Supplied &&
             first->destination->getPublicType() != TextureType::CubeMap) ||
            !isFirstSuppliedMipUpload(index, *first))
        {
            continue;
        }
        if (!hasCompleteSuppliedMipChain(first->destination))
        {
            return std::unexpected {
                makeError(ErrorCode::InvalidArgument, "Texture upload must provide every required face and mip level")};
        }
    }
    return {};
}

auto UploadBatchImpl::submit() -> Result<CompletionPoint>
{
    if (!_generatedEnvironmentWrites.empty())
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Generated environment upload needs all six base-level faces")};
    }
    if (const auto valid = validateSuppliedMipChains(); !valid)
    {
        return std::unexpected {valid.error()};
    }
    auto point = _manager.submit(_writes, _stagingPages);
    if (point)
    {
        commitUploadReservations(_writes, _id);
        _writes.clear();
        _stagingPages.clear();
        _stagingOffset = 0;
    }
    return point;
}
}
