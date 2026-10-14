#pragma once

// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <gmock/gmock.h>

#include <cstdint>
#include <panda/CompletionPoint.hpp>
#include <panda/Error.hpp>

namespace panda::test
{
class SubmissionQueueMock
{
public:
    // gMock cannot generate trailing-return declarations.
    // NOLINTBEGIN(modernize-use-trailing-return-type)
    MOCK_METHOD(void, construct, (const vk::raii::Device&, std::uint32_t), ());
    MOCK_METHOD(panda::Result<void>, initialize, (vk::PhysicalDevice), ());
    MOCK_METHOD(panda::Result<void>, prepareSubmit, (), ());
    MOCK_METHOD(panda::Result<std::uint64_t>, submit, (vk::CommandBuffer, vk::Semaphore, vk::Semaphore), ());
    MOCK_METHOD(panda::CompletionPoint, getPoint, (std::uint64_t), ());
    MOCK_METHOD(std::uint64_t, getCompletedValue, (), (const));
    MOCK_METHOD(bool, isDeviceLost, (), (const));
    MOCK_METHOD(panda::Result<bool>, poll, (panda::CompletionPoint), ());
    MOCK_METHOD(panda::Result<panda::WaitStatus>, wait, (panda::CompletionPoint, std::uint64_t), ());
    MOCK_METHOD(panda::Result<panda::WaitStatus>, waitValue, (std::uint64_t, std::uint64_t), ());
    // NOLINTEND(modernize-use-trailing-return-type)
};
}
