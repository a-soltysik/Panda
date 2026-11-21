#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <panda/base/Error.hpp>
#include <panda/resources/Texture.hpp>
#include <vector>

#include "VulkanHpp.hpp"
#include "resources/memory/MemoryAllocation.hpp"
#include "resources/memory/MemoryPool.hpp"
#include "upload/UploadBatchId.hpp"

namespace panda::detail
{
struct TextureResourceCreateInfo
{
    vk::Extent2D extent;
    vk::Format format;
    vk::ImageUsageFlags usage;
    std::uint32_t mipLevels {1};
    std::uint32_t arrayLayers {1};
    bool cubeCompatible {false};
    bool createArrayView {false};
};

namespace texture_resources = ::panda::core::vulkan::resources;
namespace texture_upload = ::panda::core::vulkan::upload;

// Declaration order destroys the view and bound image before releasing their allocation.
// The context pool and device outlive this state, including every shared owner.
class TextureResource final
{
public:
    [[nodiscard]] static auto getResource(const Texture& texture) -> const std::shared_ptr<TextureResource>&;
    [[nodiscard]] static auto create(const vk::raii::Device& device,
                                     texture_resources::MemoryPool& pool,
                                     const TextureDesc& description) -> Result<std::shared_ptr<TextureResource>>;
    [[nodiscard]] static auto create(const vk::raii::Device& device,
                                     texture_resources::MemoryPool& pool,
                                     const TextureDesc& description,
                                     vk::SampleCountFlagBits samples) -> Result<std::shared_ptr<TextureResource>>;
    [[nodiscard]] static auto createNative(const vk::raii::Device& device,
                                           texture_resources::MemoryPool& pool,
                                           const TextureResourceCreateInfo& description,
                                           vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1)
        -> Result<std::shared_ptr<TextureResource>>;
    [[nodiscard]] auto getExtent() const -> vk::Extent2D;
    [[nodiscard]] auto getFormat() const -> vk::Format;
    [[nodiscard]] auto getUsage() const -> vk::ImageUsageFlags;
    [[nodiscard]] auto getPublicFormat() const -> PixelFormat;
    [[nodiscard]] auto getPublicType() const -> TextureType;
    [[nodiscard]] auto getPublicUsage() const -> TextureUsage;

    [[nodiscard]] auto getMipLevels() const noexcept -> std::uint32_t
    {
        return _mipLevels;
    }

    [[nodiscard]] auto getMipPolicy() const noexcept -> TextureMipPolicy
    {
        return _mipPolicy;
    }

    [[nodiscard]] auto getSpecularEnvironmentSamplesPerTexel() const noexcept -> std::uint32_t
    {
        return _specularEnvironmentSamplesPerTexel;
    }

    [[nodiscard]] auto getAddressMode() const noexcept -> TextureAddressMode
    {
        return _addressMode;
    }

    [[nodiscard]] auto getNative() const -> vk::Image;
    [[nodiscard]] auto getNativeView() const -> vk::ImageView;
    [[nodiscard]] auto getNativeArrayView() const -> vk::ImageView;
    [[nodiscard]] auto getLayerView(std::uint32_t layer) const -> vk::ImageView;
    [[nodiscard]] auto getArrayLayers() const noexcept -> std::uint32_t;
    [[nodiscard]] auto belongsTo(const texture_resources::MemoryPool& pool) const -> bool;
    [[nodiscard]] auto isUploaded() const -> bool;
    [[nodiscard]] auto reserveUpload(texture_upload::UploadBatchId batchId) -> Result<void>;
    void cancelUpload(texture_upload::UploadBatchId batchId);
    void commitUpload(texture_upload::UploadBatchId batchId);

private:
    [[nodiscard]] static auto createBoundResource(const vk::raii::Device& device,
                                                  texture_resources::MemoryPool& pool,
                                                  const TextureResourceCreateInfo& description,
                                                  vk::SampleCountFlagBits samples)
        -> Result<std::shared_ptr<TextureResource>>;
    TextureResource(texture_resources::MemoryPool& pool,
                    vk::raii::Image image,
                    const TextureResourceCreateInfo& description,
                    vk::SampleCountFlagBits samples);
    [[nodiscard]] auto bind(const vk::raii::Device& device) -> Result<void>;
    [[nodiscard]] auto createViews(const vk::raii::Device& device, const TextureResourceCreateInfo& description)
        -> Result<void>;
    [[nodiscard]] auto createArrayViews(const vk::raii::Device& device, const TextureResourceCreateInfo& description)
        -> Result<void>;

    texture_resources::MemoryAllocation _allocation;
    vk::raii::Image _image;
    vk::raii::ImageView _view {nullptr};
    vk::raii::ImageView _arrayView {nullptr};
    std::vector<vk::raii::ImageView> _layerViews;
    texture_resources::MemoryPool& _pool;
    vk::Extent2D _extent;
    std::uint32_t _arrayLayers;
    vk::Format _format;
    vk::ImageUsageFlags _usage;
    vk::SampleCountFlagBits _samples;
    std::uint32_t _mipLevels {1};
    PixelFormat _publicFormat {PixelFormat::Undefined};
    TextureType _publicType {TextureType::Texture2D};
    TextureUsage _publicUsage {TextureUsage::None};
    TextureMipPolicy _mipPolicy {TextureMipPolicy::SingleLevel};
    std::uint32_t _specularEnvironmentSamplesPerTexel {defaultSpecularEnvironmentSamplesPerTexel};
    TextureAddressMode _addressMode {TextureAddressMode::Repeat};
    std::optional<texture_upload::UploadBatchId> _uploadBatchId;
    bool _uploaded {false};
};
}
