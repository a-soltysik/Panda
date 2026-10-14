#pragma once

/// @file
/// Shared ownership of fixed-size Vulkan 2D images.

#include <memory>
#include <vulkan/vulkan.hpp>

namespace panda
{
class Context;

namespace detail
{
class TextureResource;
}

/// @brief Creation parameters for one optimal-tiled, single-mip, single-layer 2D image.
struct TextureDesc
{
    /// Nonzero width and height in texels.
    vk::Extent2D extent {};
    /// Concrete Vulkan format; format and usage support is checked by Vulkan.
    vk::Format format {vk::Format::eUndefined};
    /// Transfer, sampled, storage, input-attachment or color/depth-attachment usage.
    vk::ImageUsageFlags usage;
};

/// @brief Copyable resource owner. Context outlives every copy.
/// Creation and final release are serialized with Context operations. Copies retain
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
    /// @brief Returns the image extent, or a zero extent for an empty handle.
    [[nodiscard]] auto getExtent() const noexcept -> vk::Extent2D;
    /// @brief Returns the image format, or eUndefined for an empty handle.
    [[nodiscard]] auto getFormat() const noexcept -> vk::Format;
    /// @brief Returns the creation usage, or no flags for an empty handle.
    [[nodiscard]] auto getUsage() const noexcept -> vk::ImageUsageFlags;
    /// @brief Releases this owner and leaves the handle empty; does not wait for GPU work.
    void reset() noexcept;

private:
    friend class Context;
    explicit Texture(std::shared_ptr<detail::TextureResource> resource);
    std::shared_ptr<detail::TextureResource> _resource;
};
}
