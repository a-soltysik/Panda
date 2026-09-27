#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace panda::log::detail
{
// Best-effort display name for common compiler signatures; original metadata is untouched.
class FunctionName final
{
public:
    explicit FunctionName(std::string_view signature) noexcept;

    [[nodiscard]] auto view() const noexcept -> std::string_view;

private:
    void consumeName(std::string_view signature) noexcept;

    void appendOperator(std::string_view name) noexcept;

    auto consumeNameCharacter(char character, std::size_t& templateDepth) noexcept -> bool;

    auto consumeOutsideTemplate(char character) noexcept -> bool;

    void shortenQualifiers() noexcept;

    void append(char character) noexcept;

    static constexpr std::size_t Capacity {128};

    std::array<char, Capacity> _text {};
    std::size_t _size {0};
    std::size_t _start {0};
    bool _truncated {false};
};
}
