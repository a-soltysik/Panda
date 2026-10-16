#pragma once

// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <gmock/gmock.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <panda/CompletionPoint.hpp>
#include <panda/Error.hpp>
#include <span>

#include "upload/UploadManager.hpp"

namespace panda::test
{
class UploadManagerMock
{
public:
    using Manager = panda::core::vulkan::upload::UploadManager;
    using Buffer = std::shared_ptr<panda::detail::BufferResource>;
    using BufferSpan = std::span<const Buffer>;
    using OperationSpan = std::span<const panda::core::vulkan::upload::UploadOperation>;

    // gMock cannot generate trailing-return declarations.
    // NOLINTBEGIN(modernize-use-trailing-return-type)
    MOCK_METHOD(void, destroy, (Manager&), ());
    MOCK_METHOD(void, retainBatch, (Manager&), ());
    MOCK_METHOD(void, releaseBatch, (Manager&), ());
    // cppcheck cannot parse gMock declarations with resource references.
    // cppcheck-suppress syntaxError
    MOCK_METHOD(bool, hasActiveBatches, (const Manager&), (const));
    MOCK_METHOD(panda::core::vulkan::upload::UploadStats, getStats, (const Manager&), (const));
    MOCK_METHOD(panda::Result<void>, collect, (Manager&), ());
    MOCK_METHOD(void, collectCompleted, (Manager&), ());
    MOCK_METHOD(panda::Result<void>, drain, (Manager&, std::uint64_t), ());
    MOCK_METHOD(panda::Result<void>,
                validateWrite,
                (const Manager&, const Buffer&, std::uint64_t, std::size_t),
                (const));
    MOCK_METHOD(panda::Result<panda::core::vulkan::upload::TextureUploadLayout>,
                validateTextureWrite,
                (const Manager&,
                 const std::shared_ptr<panda::detail::TextureResource>&,
                 const panda::TextureUploadRegion&,
                 std::size_t),
                (const));
    MOCK_METHOD(panda::Result<Buffer>, createStaging, (const Manager&, std::uint64_t), (const));
    MOCK_METHOD(panda::Result<panda::CompletionPoint>, submit, (Manager&, OperationSpan, BufferSpan), ());
    // NOLINTEND(modernize-use-trailing-return-type)
};
}
