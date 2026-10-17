// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
// clang-format on

#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <panda/CompletionPoint.hpp>
#include <panda/Error.hpp>
#include <panda/UploadBatch.hpp>
#include <span>

#include "ScopedMock.hpp"
#include "upload/UploadBatchId.hpp"
#include "upload/UploadBatchImpl.hpp"
// This definition needs the complete member type owned by UploadManager.
// NOLINTNEXTLINE(misc-include-cleaner)
#include "panda/core/UploadManagerMock.hpp"
#include "upload/UploadManager.hpp"
#include "upload/UploadOperation.hpp"
// The out-of-line destructor destroys UploadManager's unique_ptr<UploadCommand>
// member.
// NOLINTNEXTLINE(misc-include-cleaner)
#include "upload/UploadCommand.hpp"

namespace panda::core::vulkan::upload
{
UploadManager::UploadManager(const vk::raii::Device& device,
                             resources::MemoryPool& pool,
                             api_detail::SubmissionQueue& submissions,
                             std::uint32_t family)
    : _device {device},
      _pool {pool},
      _submissions {submissions},
      _family {family}
{
}

UploadManager::~UploadManager()
{
    try
    {
        test::ScopedMock<test::UploadManagerMock>::getActiveMock().destroy(*this);
    }
    catch (...)
    {
        std::terminate();
    }
}

auto UploadManager::createBatch() -> std::unique_ptr<api_detail::UploadBatchImpl>
{
    return std::make_unique<api_detail::UploadBatchImpl>(*this, UploadBatchId {_nextBatchId++});
}

auto UploadManager::hasActiveBatches() const -> bool
{
    return test::ScopedMock<test::UploadManagerMock>::getActiveMock().hasActiveBatches(*this);
}

auto UploadManager::getStats() const -> UploadStats
{
    return test::ScopedMock<test::UploadManagerMock>::getActiveMock().getStats(*this);
}

auto UploadManager::collect() -> Result<void>
{
    return test::ScopedMock<test::UploadManagerMock>::getActiveMock().collect(*this);
}

void UploadManager::collectCompleted()
{
    test::ScopedMock<test::UploadManagerMock>::getActiveMock().collectCompleted(*this);
}

auto UploadManager::drain(std::uint64_t timeout) -> Result<void>
{
    return test::ScopedMock<test::UploadManagerMock>::getActiveMock().drain(*this, timeout);
}

auto UploadManager::validateWrite(const std::shared_ptr<api_detail::BufferResource>& destination,
                                  std::uint64_t offset,
                                  std::size_t size) const -> Result<void>
{
    return test::ScopedMock<test::UploadManagerMock>::getActiveMock().validateWrite(*this, destination, offset, size);
}

auto UploadManager::validateTextureWrite(const std::shared_ptr<api_detail::TextureResource>& destination,
                                         const TextureUploadRegion& region,
                                         std::size_t size) const -> Result<TextureUploadLayout>
{
    return test::ScopedMock<test::UploadManagerMock>::getActiveMock().validateTextureWrite(*this,
                                                                                           destination,
                                                                                           region,
                                                                                           size);
}

auto UploadManager::createStaging(std::uint64_t size) const -> Result<std::shared_ptr<api_detail::BufferResource>>
{
    return test::ScopedMock<test::UploadManagerMock>::getActiveMock().createStaging(*this, size);
}

auto UploadManager::submit(std::span<const UploadOperation> writes,
                           std::span<const std::shared_ptr<api_detail::BufferResource>> stagingPages)
    -> Result<CompletionPoint>
{
    return test::ScopedMock<test::UploadManagerMock>::getActiveMock().submit(*this, writes, stagingPages);
}

void UploadManager::retainBatch()
{
    test::ScopedMock<test::UploadManagerMock>::getActiveMock().retainBatch(*this);
}

void UploadManager::releaseBatch()
{
    test::ScopedMock<test::UploadManagerMock>::getActiveMock().releaseBatch(*this);
}
}
