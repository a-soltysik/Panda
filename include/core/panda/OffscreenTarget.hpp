#pragma once

/// @file
/// Explicit single-sample SDR render targets and RGBA pixel readback data.

#include <cstdint>
#include <memory>
#include <vector>
#include <vulkan/vulkan.hpp>

namespace panda
{
class Context;

namespace detail
{
class OffscreenTargetResource;
}

/// @brief Fixed-format offscreen color target creation parameters.
struct OffscreenTargetDesc
{
    /// Nonzero target extent in pixels.
    vk::Extent2D extent {};
};

/// @brief Tightly packed pixels copied from an offscreen target.
struct ImageReadback
{
    /// Pixel extent copied from the target.
    vk::Extent2D extent {};
    /// Top-to-bottom rows in RGBA order. RGB channels are sRGB encoded; alpha is linear UNORM.
    std::vector<std::uint8_t> rgba;
};

/// @brief Copyable owner of a single-sample SDR offscreen color target. Context
/// outlives every copy; depth and in-flight use are retained by the renderer.
class OffscreenTarget final
{
public:
    /// @brief Creates an empty handle.
    OffscreenTarget();
    /// @brief Retains the same target without copying its pixels.
    OffscreenTarget(const OffscreenTarget& other);
    /// @brief Releases the previous target and retains other's target.
    auto operator=(const OffscreenTarget& other) -> OffscreenTarget&;
    /// @brief Transfers ownership and leaves other empty.
    OffscreenTarget(OffscreenTarget&& other) noexcept;
    /// @brief Releases the previous target and transfers ownership.
    auto operator=(OffscreenTarget&& other) noexcept -> OffscreenTarget&;
    /// @brief Releases one owner; the last owner destroys the color image and its allocation.
    ~OffscreenTarget();

    /// @brief Reports whether this handle retains a target.
    [[nodiscard]] auto isValid() const noexcept -> bool;
    /// @brief Returns the target extent, or a zero extent for an empty handle.
    [[nodiscard]] auto getExtent() const noexcept -> vk::Extent2D;
    /// @brief Returns the target format, or eUndefined for an empty handle.
    [[nodiscard]] auto getFormat() const noexcept -> vk::Format;
    /// @brief Releases this owner and leaves the handle empty; submitted work retains its use.
    void reset() noexcept;

private:
    friend class Context;
    friend class detail::OffscreenTargetResource;
    explicit OffscreenTarget(std::shared_ptr<detail::OffscreenTargetResource> resource);
    std::shared_ptr<detail::OffscreenTargetResource> _resource;
};
}
