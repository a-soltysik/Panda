#pragma once

/// @file
/// Shared ownership of fixed-size 2D images and cubemaps.

#include <bit>
#include <cstdint>
#include <glm/vec2.hpp>
#include <memory>
#include <panda/resources/PixelFormat.hpp>

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

/// @brief How a texture's mip chain is supplied during upload.
enum class TextureMipPolicy : std::uint8_t
{
    /// Allocate only the base level.
    SingleLevel,
    /// Allocate and upload every level in the full chain.
    Supplied,
    /// Generate 2D mip levels by downsampling RGBA8/BGRA8 UNORM or sRGB images.
    Generate,
    /// Generate cubemap roughness mips with GGX specular environment filtering.
    GenerateSpecularEnvironment,
};

/// @brief Image view type created for a public texture.
enum class TextureType : std::uint8_t
{
    /// A two-dimensional image with one array layer.
    Texture2D,
    /// A square, six-face linear HDR image; uploads use Vulkan cube-face layer order.
    CubeMap,
};

/// @brief Addressing at one edge of a sampled 2D texture.
enum class TextureAddressMode : std::uint8_t
{
    Repeat,
    MirroredRepeat,
    ClampToEdge,
};

namespace detail
{
inline constexpr auto defaultSpecularEnvironmentSamplesPerTexel = std::uint32_t {128};
class TextureResource;
}

/// @brief Creation parameters for one optimal-tiled, single-layer 2D image.
struct TextureDesc
{
    /// Nonzero extent in texels; x is width and y is height.
    glm::uvec2 extent;
    /// Concrete format from Panda's supported catalog.
    PixelFormat format {PixelFormat::Undefined};
    /// Combination of supported texture operations; must include Sampled for its default view.
    TextureUsage usage {TextureUsage::None};
    /// Single level by default; supplied and generated policies allocate the full mip chain.
    TextureMipPolicy mipPolicy {TextureMipPolicy::SingleLevel};
    /// GGX importance samples per output texel; used only by GenerateSpecularEnvironment.
    std::uint32_t specularEnvironmentSamplesPerTexel {detail::defaultSpecularEnvironmentSamplesPerTexel};
    /// How UV coordinates outside [0,1] are handled on both axes.
    TextureAddressMode addressMode {TextureAddressMode::Repeat};
    /// Image view type; CubeMap requires square Rgba16Float data.
    TextureType type {TextureType::Texture2D};
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
    /// @brief Returns the view type, or Texture2D for an empty handle.
    [[nodiscard]] auto getType() const noexcept -> TextureType;
    /// @brief Returns the creation usage, or None for an empty handle.
    [[nodiscard]] auto getUsage() const noexcept -> TextureUsage;
    /// @brief Returns the allocated mip-level count, or zero for an empty handle.
    [[nodiscard]] auto getMipLevels() const noexcept -> std::uint32_t;
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
