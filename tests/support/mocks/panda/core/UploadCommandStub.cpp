// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <cstdint>
#include <expected>
#include <memory>
#include <panda/Error.hpp>
#include <span>
#include <vector>

#include "BufferResource.hpp"
#include "ScopedMock.hpp"
#include "panda/core/UploadCommandMock.hpp"
#include "upload/UploadCommand.hpp"
#include "upload/UploadOperation.hpp"

namespace panda::core::vulkan::upload
{
UploadCommand::UploadCommand(std::span<const UploadOperation> writes,
                             std::span<const std::shared_ptr<api_detail::BufferResource>> stagingPages)
    : _writes {writes.begin(), writes.end()},
      _stagingPages {stagingPages.begin(), stagingPages.end()}
{
}

auto UploadCommand::create(const vk::raii::Device& device,
                           std::uint32_t family,
                           std::span<const UploadOperation> writes,
                           std::span<const std::shared_ptr<api_detail::BufferResource>> stagingPages)
    -> Result<std::unique_ptr<UploadCommand>>
{
    const auto created =
        test::ScopedMock<test::UploadCommandMock>::getActiveMock().create(device, family, writes, stagingPages);
    if (!created)
    {
        return std::unexpected {created.error()};
    }
    return std::unique_ptr<UploadCommand> {
        new UploadCommand {writes, stagingPages}
    };
}

// The test replacement must keep this non-static production member signature.
// cppcheck-suppress functionStatic
// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
auto UploadCommand::initialize([[maybe_unused]] const vk::raii::Device& device, [[maybe_unused]] std::uint32_t family)
    -> Result<void>
{
    return {};
}

// cppcheck-suppress functionStatic
// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
auto UploadCommand::record() const -> Result<void>
{
    return {};
}

auto UploadCommand::getCommand() const -> vk::CommandBuffer
{
    return test::ScopedMock<test::UploadCommandMock>::getActiveMock().getCommand(*this);
}

auto UploadCommand::getStagingSize() const -> std::uint64_t
{
    return test::ScopedMock<test::UploadCommandMock>::getActiveMock().getStagingSize(*this);
}
}
