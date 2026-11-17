#pragma once

/// @file
/// Shared ownership of fixed-size 2D images.

#include <bit>
#include <cstdint>
#include <glm/vec2.hpp>
#include <memory>
#include <panda/PixelFormat.hpp>

namespace panda
{
class ResourceManager;

/// @brief Supported operations for a public texture.
enum class TextureUsage : std::uint8_t
{
    /// No operation; texture creation rejects this value.
    None = 0,
    /// Texture may receive data through UploadBatch.
    TransferDestination = 1U << 0U,
    /// Texture may be sampled by a shader.
    Sampled = 1U << 1U,
};

namespace detail
{
class TextureResource;
}

/// @brief Creation parameters for one optimal-tiled, single-mip, single-layer 2D image.
struct TextureDesc
{
    /// Nonzero extent in texels; x is width and y is height.
    glm::uvec2 extent;
    /// Concrete format from Panda's supported catalog.
    PixelFormat format {PixelFormat::Undefined};
    /// Combination of supported texture operations; must include Sampled for its default view.
    TextureUsage usage {TextureUsage::None};
};

/// @brief Copyable resource owner. Engine outlives every copy.
/// Creation and final release are serialized with Engine operations. Copies retain
/// the same image and do not synchronize GPU use; no GPU work is submitted here.
class Texture final
{
public:
    /// @brief Creates an empty handle.
    Texture();
    /// @brief Retains the same image without copying its contents.
    Texture(const Texture& other);
    /// @brief Releases the previous image and retains other's image.
    auto operator=(const Texture& other) -> Texture&;
    /// @brief Transfers ownership and leaves other empty.
    Texture(Texture&& other) noexcept;
    /// @brief Releases the previous image and transfers ownership.
    auto operator=(Texture&& other) noexcept -> Texture&;
    /// @brief Releases one owner; the last owner destroys the image before its allocation.
    ~Texture();
    /// @brief Reports whether this handle retains an image.
    [[nodiscard]] auto isValid() const noexcept -> bool;
    /// @brief Returns the image extent in texels (x is width, y is height), or zero for an empty handle.
    [[nodiscard]] auto getExtent() const noexcept -> glm::uvec2;
    /// @brief Returns the selected format, or Undefined for an empty handle.
    [[nodiscard]] auto getFormat() const noexcept -> PixelFormat;
    /// @brief Returns the creation usage, or None for an empty handle.
    [[nodiscard]] auto getUsage() const noexcept -> TextureUsage;
    /// @brief Releases this owner and leaves the handle empty; does not wait for GPU work.
    void reset() noexcept;

private:
    friend class ResourceManager;
    friend class detail::TextureResource;
    explicit Texture(std::shared_ptr<detail::TextureResource> resource);
    std::shared_ptr<detail::TextureResource> _resource;
};

/// @brief Combines two texture operation flags.
[[nodiscard]] constexpr auto operator|(TextureUsage left, TextureUsage right) noexcept -> TextureUsage
{
    return std::bit_cast<TextureUsage>(
        static_cast<std::uint8_t>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right)));
}

/// @brief Returns the texture operation flags present in both values.
[[nodiscard]] constexpr auto operator&(TextureUsage left, TextureUsage right) noexcept -> TextureUsage
{
    return std::bit_cast<TextureUsage>(
        static_cast<std::uint8_t>(static_cast<std::uint32_t>(left) & static_cast<std::uint32_t>(right)));
}

/// @brief Adds texture operation flags to the left-hand value.
constexpr auto operator|=(TextureUsage& left, TextureUsage right) noexcept -> TextureUsage&
{
    left = left | right;
    return left;
}
}
