#pragma once

/// @file
/// Host hardware diagnostics.

#include <optional>
#include <string>

namespace panda
{
/// @brief Hardware information about the machine running Panda.
struct HostInfo
{
    /// Human-readable CPU model, or empty when the operating system does not provide it.
    std::optional<std::string> cpuModel;
};

/// @brief Reads host hardware information supported by the current platform.
/// @return Available host information. Does not create a Context or require a window.
[[nodiscard]] auto getHostInfo() -> HostInfo;
}
