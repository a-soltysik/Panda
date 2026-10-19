#include <optional>
#include <panda/HostInfo.hpp>
#include <string>
#include <string_view>

#ifdef _WIN32
#    define NOMINMAX
#    include <Windows.h>  // IWYU pragma: keep
#    include <minwindef.h>
#    include <stringapiset.h>
#    include <winerror.h>
#    include <winnls.h>
#    include <winreg.h>

#    include <limits>
#elifdef __linux__
#    include <fstream>
#endif

namespace panda
{
namespace
{
auto trim(std::string_view value) -> std::optional<std::string>
{
    constexpr auto whitespace = " \t\r\n";
    const auto first = value.find_first_not_of(whitespace);
    if (first == std::string::npos)
    {
        return std::nullopt;
    }
    const auto last = value.find_last_not_of(whitespace);
    return std::string {value.substr(first, last - first + 1)};
}

#ifdef _WIN32
auto readCpuNameFromRegistry() -> std::optional<std::wstring>
{
    constexpr auto key = L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0";
    constexpr auto value = L"ProcessorNameString";
    auto bufferSize = DWORD {};
    if (RegGetValueW(HKEY_LOCAL_MACHINE, key, value, RRF_RT_REG_SZ, nullptr, nullptr, &bufferSize) != ERROR_SUCCESS ||
        bufferSize < sizeof(wchar_t))
    {
        return std::nullopt;
    }

    auto model = std::wstring(bufferSize / sizeof(wchar_t), L'\0');
    if (RegGetValueW(HKEY_LOCAL_MACHINE, key, value, RRF_RT_REG_SZ, nullptr, model.data(), &bufferSize) !=
        ERROR_SUCCESS)
    {
        return std::nullopt;
    }
    while (!model.empty() && model.back() == L'\0')
    {
        model.pop_back();
    }
    return model.empty() ? std::nullopt : std::optional {model};
}

auto convertToUtf8(const std::wstring& model) -> std::optional<std::string>
{
    if (model.empty() || model.size() > static_cast<std::wstring::size_type>(std::numeric_limits<int>::max()))
    {
        return std::nullopt;
    }
    const auto inputLength = static_cast<int>(model.size());
    const auto outputLength =
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, model.c_str(), inputLength, nullptr, 0, nullptr, nullptr);
    if (outputLength <= 0)
    {
        return std::nullopt;
    }
    auto result = std::string(static_cast<std::string::size_type>(outputLength), '\0');
    if (WideCharToMultiByte(CP_UTF8,
                            WC_ERR_INVALID_CHARS,
                            model.c_str(),
                            inputLength,
                            result.data(),
                            outputLength,
                            nullptr,
                            nullptr) != outputLength)
    {
        return std::nullopt;
    }
    return result;
}

auto readCpuModel() -> std::optional<std::string>
{
    const auto wideModel = readCpuNameFromRegistry();
    if (!wideModel)
    {
        return std::nullopt;
    }
    const auto model = convertToUtf8(*wideModel);
    return model ? trim(*model) : std::nullopt;
}
#elifdef __linux__
auto readCpuModel() -> std::optional<std::string>
{
    auto input = std::ifstream {"/proc/cpuinfo"};
    for (auto line = std::string {}; std::getline(input, line);)
    {
        const auto separator = line.find(':');
        if (separator == std::string::npos)
        {
            continue;
        }
        const auto name = trim(std::string_view {line}.substr(0, separator));
        if (name == "model name")
        {
            return trim(std::string_view {line}.substr(separator + 1));
        }
    }
    return std::nullopt;
}
#else
auto readCpuModel() -> std::optional<std::string>
{
    return std::nullopt;
}
#endif
}

auto getHostInfo() -> HostInfo
{
    return HostInfo {.cpuModel = readCpuModel()};
}
}
