// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <cstdint>
#include <panda/CompletionPoint.hpp>
#include <panda/Error.hpp>

#include "ScopedMock.hpp"
#include "SubmissionQueue.hpp"
#include "SubmissionQueueMock.hpp"

namespace panda::detail
{
// Link-time replacements must keep the production member signatures.
// NOLINTBEGIN(readability-convert-member-functions-to-static)
SubmissionQueue::SubmissionQueue(const vk::raii::Device& device, std::uint32_t family)
    : _device {device},
      _queue {nullptr}
{
    test::ScopedMock<test::SubmissionQueueMock>::getActiveMock().construct(device, family);
}

// cppcheck-suppress functionStatic
auto SubmissionQueue::initialize(vk::PhysicalDevice physicalDevice) -> Result<void>
{
    return test::ScopedMock<test::SubmissionQueueMock>::getActiveMock().initialize(physicalDevice);
}

// cppcheck-suppress functionStatic
auto SubmissionQueue::prepareSubmit() -> Result<void>
{
    return test::ScopedMock<test::SubmissionQueueMock>::getActiveMock().prepareSubmit();
}

// cppcheck-suppress functionStatic
auto SubmissionQueue::submit(vk::CommandBuffer command, vk::Semaphore acquire, vk::Semaphore finished)
    -> Result<std::uint64_t>
{
    return test::ScopedMock<test::SubmissionQueueMock>::getActiveMock().submit(command, acquire, finished);
}

// cppcheck-suppress functionStatic
auto SubmissionQueue::getPoint(std::uint64_t value) const -> CompletionPoint
{
    return test::ScopedMock<test::SubmissionQueueMock>::getActiveMock().getPoint(value);
}

// cppcheck-suppress functionStatic
auto SubmissionQueue::getCompletedValue() const -> std::uint64_t
{
    return test::ScopedMock<test::SubmissionQueueMock>::getActiveMock().getCompletedValue();
}

// cppcheck-suppress functionStatic
auto SubmissionQueue::isDeviceLost() const -> bool
{
    return test::ScopedMock<test::SubmissionQueueMock>::getActiveMock().isDeviceLost();
}

// cppcheck-suppress functionStatic
auto SubmissionQueue::poll(CompletionPoint point) -> Result<bool>
{
    return test::ScopedMock<test::SubmissionQueueMock>::getActiveMock().poll(point);
}

// cppcheck-suppress functionStatic
auto SubmissionQueue::wait(CompletionPoint point, std::uint64_t timeout) -> Result<WaitStatus>
{
    return test::ScopedMock<test::SubmissionQueueMock>::getActiveMock().wait(point, timeout);
}

// cppcheck-suppress functionStatic
auto SubmissionQueue::waitValue(std::uint64_t value, std::uint64_t timeout) -> Result<WaitStatus>
{
    return test::ScopedMock<test::SubmissionQueueMock>::getActiveMock().waitValue(value, timeout);
}

// NOLINTEND(readability-convert-member-functions-to-static)
}
