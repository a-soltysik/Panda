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
    /// @brief Window-frame outcome.
    enum class Status : std::uint8_t
    {
        Presented,
        Skipped,
        Suspended,
        RecreatePending,
        /// Offscreen work was submitted; no window frame was presented.
        Rendered
    };

    /// Outcome; RecreatePending can still carry an already submitted frame.
    Status status {Status::Skipped};
    /// Committed work; empty when no GPU work was submitted. Completion does not mean work or presentation finished.
    CompletionPoint completion;
    /// Selected color/depth sample count: e1, e2 or e4. A mesh view initializes the renderer and reports its choice.
    vk::SampleCountFlagBits effectiveSampleCount {vk::SampleCountFlagBits::e1};
};
}
