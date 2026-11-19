#pragma once

/// @file
/// Result of one rendering operation.

#include <cstdint>
#include <panda/rendering/SampleCount.hpp>  // IWYU pragma: export
#include <panda/submission/CompletionPoint.hpp>

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
    /// Color/depth sample count used by this submission. Clear-only views use one sample.
    /// Meaningful when completion is valid.
    SampleCount effectiveSampleCount {SampleCount::One};
};
}
