#pragma once

/// @file
/// Result of one rendering operation.

#include <cstdint>
#include <panda/CompletionPoint.hpp>
#include <vulkan/vulkan.hpp>

namespace panda
{
/// @brief Result of one rendering operation.
struct FrameResult
{
    /// @brief Outcome of an offscreen or window rendering operation.
    enum class Status : std::uint8_t
    {
        /// Offscreen submission succeeded, or a window frame was accepted without requiring recreation.
        Rendered,
        /// No rendering work was submitted for this call.
        Skipped,
        /// The window has no usable extent, so no rendering work was submitted.
        Suspended,
        /// Swapchain recreation is needed; completion may still identify submitted work.
        RecreatePending,
    };

    /// Outcome. RecreatePending can still carry an already submitted frame.
    Status status {Status::Skipped};
    /// Submitted work; valid when GPU work was committed, even if status is RecreatePending.
    /// Completion does not mean work or presentation has finished.
    CompletionPoint completion;
    /// Selected color/depth sample count: e1, e2 or e4. A mesh view initializes the renderer and reports its choice.
    vk::SampleCountFlagBits effectiveSampleCount {vk::SampleCountFlagBits::e1};
};
}
