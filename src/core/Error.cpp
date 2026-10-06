#include <optional>
#include <panda/Error.hpp>
#include <source_location>
#include <string>
#include <utility>

namespace panda
{

auto makeError(ErrorCode code, std::string message, std::optional<NativeError> native, std::source_location source)
    -> Error
{
    return {.code = code, .message = std::move(message), .native = std::move(native), .source = source};
}

}
