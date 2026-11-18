#include "Stacktrace.hpp"

#include <string>

#ifdef PANDA_ENABLE_STACKTRACE
#    include <format>
#    include <stacktrace>

#    include "FunctionName.hpp"
#    include "SourcePath.hpp"
#endif

namespace panda::log::detail
{
auto stacktraceText() -> std::string
{
#ifdef PANDA_ENABLE_STACKTRACE
    static constexpr auto maximumFrames = 32UZ;
    const auto trace = std::stacktrace::current(0UZ, maximumFrames);
    if (trace.empty())
    {
        return "\n[STACKTRACE unavailable]\n";
    }
    auto text = std::string {"\n[STACKTRACE]\n"};
    for (const auto& frame : trace)
    {
        const auto file = frame.source_file();
        if (file.empty())
        {
            text += std::format("  {}\n", std::to_string(frame));
        }
        else
        {
            const auto function = FunctionName {frame.description()};
            text += std::format("  {}:{} ({})\n", relativeSourcePath(file), frame.source_line(), function.view());
        }
    }
    return text;
#else
    return {};
#endif
}
}
