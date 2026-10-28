#pragma once

// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <array>
#include <cstdint>
#include <panda/CompletionPoint.hpp>
#include <panda/Error.hpp>
#include <utility>

namespace panda::detail
{
// One serialized graphics/compute submission clock per Context. The borrowed
// device outlives it. It owns no resource packets; callers retain submitted uses.
class SubmissionQueue final
{
public:
    SubmissionQueue(const vk::raii::Device& device, std::uint32_t family);
    ~SubmissionQueue();
    SubmissionQueue(const SubmissionQueue&) = delete;
    auto operator=(const SubmissionQueue&) -> SubmissionQueue& = delete;
    SubmissionQueue(SubmissionQueue&&) = delete;
    auto operator=(SubmissionQueue&&) -> SubmissionQueue& = delete;
    [[nodiscard]] auto initialize(vk::PhysicalDevice physicalDevice) -> Result<void>;
    [[nodiscard]] auto prepareSubmit() -> Result<void>;
    [[nodiscard]] auto submit(vk::CommandBuffer command, vk::Semaphore acquire, vk::Semaphore finished)
        -> Result<std::uint64_t>;
    [[nodiscard]] auto submitExternalWait(vk::CommandBuffer command, vk::Semaphore semaphore, std::uint64_t value)
        -> Result<std::uint64_t>;
    [[nodiscard]] auto submitExternalSignal(vk::CommandBuffer command, vk::Semaphore semaphore, std::uint64_t value)
        -> Result<std::uint64_t>;
    [[nodiscard]] auto getPoint(std::uint64_t value) const -> CompletionPoint;
    [[nodiscard]] auto getCompletedValue() const -> std::uint64_t;
    [[nodiscard]] auto isDeviceLost() const -> bool;
    [[nodiscard]] auto poll(CompletionPoint point) -> Result<bool>;
    [[nodiscard]] auto wait(CompletionPoint point, std::uint64_t timeout) -> Result<WaitStatus>;
    [[nodiscard]] auto waitValue(std::uint64_t value, std::uint64_t timeout) -> Result<WaitStatus>;

private:
    struct SubmissionSync
    {
        vk::Semaphore acquire {};
        vk::Semaphore finished {};
        vk::SemaphoreSubmitInfo externalWait {};
        vk::SemaphoreSubmitInfo externalSignal {};
    };

    using WaitInfos = std::pair<std::array<vk::SemaphoreSubmitInfo, 2>, std::uint32_t>;
    using SignalInfos = std::pair<std::array<vk::SemaphoreSubmitInfo, 3>, std::uint32_t>;

    [[nodiscard]] auto validatePoint(CompletionPoint point) const -> Result<void>;
    [[nodiscard]] auto refreshCompleted() -> Result<void>;
    [[nodiscard]] auto ensureOutstandingCapacity() -> Result<void>;
    [[nodiscard]] static auto makeWaitInfos(const SubmissionSync& sync) -> WaitInfos;
    [[nodiscard]] auto makeSignalInfos(const SubmissionSync& sync, std::uint64_t value) const -> SignalInfos;
    [[nodiscard]] auto submitValue(vk::CommandBuffer command, const SubmissionSync& sync, std::uint64_t value) const
        -> vk::Result;
    [[nodiscard]] auto submit(vk::CommandBuffer command, const SubmissionSync& sync) -> Result<std::uint64_t>;

    const vk::raii::Device& _device;
    vk::raii::Queue _queue;
    vk::raii::Semaphore _timeline {nullptr};
    std::uint64_t _id {0};
    std::uint64_t _committed {0};
    std::uint64_t _completed {0};
    std::uint64_t _maximumDifference {0};
    bool _deviceLost {false};
};
}
