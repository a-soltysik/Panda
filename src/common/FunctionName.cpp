#include "FunctionName.hpp"

#include <cstddef>
#include <string_view>

namespace panda::log::detail
{
FunctionName::FunctionName(std::string_view signature) noexcept
{
    signature = signature.substr(0, signature.find(" ["));
    if (signature.contains("<lambda") || signature.contains("(anonymous class)") || signature.contains("(lambda"))
    {
        for (const auto character : std::string_view {"<lambda>"})
        {
            append(character);
        }
        return;
    }
    const auto operation = signature.rfind("operator");
    if (operation != std::string_view::npos)
    {
        appendOperator(signature.substr(operation));
        return;
    }
    consumeName(signature);
    shortenQualifiers();
    if (_size == 0)
    {
        append('?');
    }
}

void FunctionName::consumeName(std::string_view signature) noexcept
{
    auto templateDepth = 0UZ;
    auto remaining = signature;
    static constexpr auto AnonymousNamespace = std::string_view {"(anonymous namespace)::"};
    while (!remaining.empty())
    {
        if (remaining.starts_with(AnonymousNamespace))
        {
            remaining.remove_prefix(AnonymousNamespace.size());
        }
        else if (consumeNameCharacter(remaining.front(), templateDepth))
        {
            remaining.remove_prefix(1);
        }
        else
        {
            break;
        }
    }
}

void FunctionName::appendOperator(std::string_view name) noexcept
{
    auto parameters = name.find('(');
    if (name.starts_with("operator()") || name.starts_with("operator ()"))
    {
        parameters = name.find('(', parameters + 2);
    }
    for (const auto character : name.substr(0, parameters))
    {
        append(character);
    }
}

auto FunctionName::consumeNameCharacter(char character, std::size_t& templateDepth) noexcept -> bool
{
    if (character == '<')
    {
        ++templateDepth;
    }
    else if (character == '>' && templateDepth != 0)
    {
        --templateDepth;
    }
    else if (templateDepth == 0)
    {
        return consumeOutsideTemplate(character);
    }
    return true;
}

auto FunctionName::consumeOutsideTemplate(char character) noexcept -> bool
{
    if (character == '(')
    {
        return false;
    }
    if (character == ' ' || character == '\t')
    {
        _size = 0;
        _truncated = false;
    }
    else
    {
        append(character);
    }
    return true;
}

void FunctionName::shortenQualifiers() noexcept
{
    const auto name = std::string_view {_text.data(), _size};
    const auto last = name.rfind("::");
    if (last != std::string_view::npos && last != 0)
    {
        const auto previous = name.rfind("::", last - 1);
        if (previous != std::string_view::npos)
        {
            _start = previous + 2;
        }
    }
    while (_start < _size && (name.at(_start) == '*' || name.at(_start) == '&'))
    {
        ++_start;
    }
}

void FunctionName::append(char character) noexcept
{
    if (_truncated)
    {
        return;
    }
    if (_size < _text.size())
    {
        _text.at(_size++) = character;
    }
    else
    {
        _truncated = true;
        _text.at(_text.size() - 3) = '.';
        _text.at(_text.size() - 2) = '.';
        _text.at(_text.size() - 1) = '.';
    }
}

auto FunctionName::view() const noexcept -> std::string_view
{
    return std::string_view {_text.data(), _size}.substr(_start);
}
}
