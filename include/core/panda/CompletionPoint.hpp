#pragma once

/// @file
/// Opaque completion of work submitted by one Context.

#include <cstdint>

namespace panda
{
namespace detail
{
class SubmissionQueue;
}

/// @brief Copyable completion token; does not retain its Context or any resource.
/// A default token represents no submitted work. Context outlives all uses of a
/// token. Values are issued only after successful submission and survive Context moves.
class CompletionPoint final
{
public:
    /// @brief Creates an empty token; poll/wait reject empty tokens.
    CompletionPoint();
    /// @brief Reports whether this token identifies submitted work.
    [[nodiscard]] auto isValid() const noexcept -> bool;

private:
    friend class detail::SubmissionQueue;
    std::uint64_t _contextId {0};
    std::uint64_t _value {0};
};

/// @brief Outcome of an explicit completion wait; timeout is not completion.
enum class WaitStatus : std::uint8_t
{
    /// All Vulkan work protected by the token completed.
    Completed,
    /// The deadline expired; submitted work may still be pending.
    Timeout
};
}
