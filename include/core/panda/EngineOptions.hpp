#pragma once

/// @file
/// Vulkan context creation options.

#include <cstdint>
#include <string>

namespace panda
{
/// @brief Options for creating a Vulkan 1.3 context.
struct EngineOptions
{
    /// Default maximum number of lights accepted by the built-in renderer.
    static constexpr std::uint32_t defaultMaxLights {64};

    /// Whether external Vulkan buffers are unavailable, enabled when supported,
    /// or required on the selected device.
    enum class ExternalBufferSupport : std::uint8_t
    {
        Disabled,
        Optional,
        Required
    };

    /// Name passed to the Vulkan loader; copied during creation.
    std::string applicationName {"Panda"};
    /// Enable the installed Khronos validation layer and route its debug-utils diagnostics
    /// to the Panda logger; failure is reported if the layer or debug-utils extension is absent.
    bool enableValidation {false};
    /// Create timestamp query pools and collect paired CPU and GPU frame timings when the selected queue supports them.
    /// Disabled by default to avoid measurement work in contexts that do not need it.
    bool enableFramePerformanceSampling {false};
    /// Maximum lights in one RenderView; zero allows views without direct lighting.
    /// Positive limits must fit the selected device's storage-buffer limits.
    std::uint32_t maxLights {defaultMaxLights};
    /// External Vulkan memory export support.
    ExternalBufferSupport externalBufferSupport {ExternalBufferSupport::Disabled};
};
}
