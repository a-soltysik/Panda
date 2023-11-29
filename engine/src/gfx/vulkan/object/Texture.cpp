#define STB_IMAGE_IMPLEMENTATION

#include "panda/gfx/vulkan/object/Texture.h"

#include <stb_image.h>

#include "panda/gfx/vulkan/Buffer.h"
#include "panda/gfx/vulkan/CommandBuffer.h"
#include "panda/gfx/vulkan/Context.h"

namespace panda::gfx::vulkan
{

namespace
{
auto copyBufferToImage(const Device& device, const Buffer& buffer, vk::Image image, uint32_t width, uint32_t height)
    -> void
{
    const auto commandBuffer = CommandBuffer::beginSingleTimeCommandBuffer(device);

    const auto region = vk::BufferImageCopy {
        0,
        0,
        0,
        {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
        {0, 0, 0},
        {width, height, 1}
    };
    commandBuffer.copyBufferToImage(buffer.buffer, image, vk::ImageLayout::eTransferDstOptimal, region);
    CommandBuffer::endSingleTimeCommandBuffer(device, commandBuffer);
}

auto transitionImageLayout(const Device& device, vk::Image image, vk::ImageLayout oldLayout, vk::ImageLayout newLayout)
    -> void
{
    const auto commandBuffer = CommandBuffer::beginSingleTimeCommandBuffer(device);

    auto barrier = vk::ImageMemoryBarrier {
        {},
        {},
        oldLayout,
        newLayout,
        vk::QueueFamilyIgnored,
        vk::QueueFamilyIgnored,
        image,
        {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}
    };

    auto sourceStage = vk::PipelineStageFlags {};
    auto destinationStage = vk::PipelineStageFlags {};

    if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eTransferDstOptimal)
    {
        barrier.srcAccessMask = {};
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
        sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;
        destinationStage = vk::PipelineStageFlagBits::eTransfer;
    }
    else if (oldLayout == vk::ImageLayout::eTransferDstOptimal && newLayout == vk::ImageLayout::eShaderReadOnlyOptimal)
    {
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

        sourceStage = vk::PipelineStageFlagBits::eTransfer;
        destinationStage = vk::PipelineStageFlagBits::eFragmentShader;
    }

    commandBuffer.pipelineBarrier(sourceStage, destinationStage, {}, {}, {}, barrier);

    CommandBuffer::endSingleTimeCommandBuffer(device, commandBuffer);
}

auto createTextureImageView(const Device& device, vk::Image image)
{
    const auto viewInfo = vk::ImageViewCreateInfo {
        {},
        image,
        vk::ImageViewType::e2D,
        vk::Format::eR8G8B8A8Srgb,
        {},
        {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}
    };

    return expect(device.logicalDevice.createImageView(viewInfo), vk::Result::eSuccess, "Failed to create image view");
}

auto createTextureSampler(const Device& device) -> vk::Sampler
{
    const auto samplerInfo = vk::SamplerCreateInfo {{},
                                                    vk::Filter::eLinear,
                                                    vk::Filter::eLinear,
                                                    vk::SamplerMipmapMode::eLinear,
                                                    vk::SamplerAddressMode::eRepeat,
                                                    vk::SamplerAddressMode::eRepeat,
                                                    vk::SamplerAddressMode::eRepeat,
                                                    0.F,
                                                    vk::True,
                                                    device.physicalDevice.getProperties().limits.maxSamplerAnisotropy,
                                                    vk::False,
                                                    vk::CompareOp::eAlways,
                                                    0.F,
                                                    0.F,
                                                    vk::BorderColor::eIntOpaqueBlack,
                                                    vk::False};

    return expect(device.logicalDevice.createSampler(samplerInfo), vk::Result::eSuccess, "Failed to create sampler");
}
}

Texture::Texture(const Context& context, const std::filesystem::path& path)
    : _context {context}
{
    auto width = int {};
    auto height = int {};
    auto channels = int {};
    auto* pixels = stbi_load(path.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
    const auto size = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
    const auto data = std::span {pixels, size};

    load(std::vector<char> {data.begin(), data.end()}, width, height);
    stbi_image_free(pixels);
}

Texture::~Texture()
{
    _context.getDevice().logicalDevice.destroy(_sampler);
    _context.getDevice().logicalDevice.destroy(_imageView);
    _context.getDevice().logicalDevice.destroy(_image);
    _context.getDevice().logicalDevice.free(_imageMemory);
}

auto Texture::getDescriptorImageInfo() const noexcept -> vk::DescriptorImageInfo
{
    return vk::DescriptorImageInfo {_sampler, _imageView, vk::ImageLayout::eShaderReadOnlyOptimal};
}

auto Texture::getDefaultTexture(const Context& context, glm::vec4 color) -> std::unique_ptr<Texture>
{
    return std::make_unique<Texture>(
        context,
        std::array {static_cast<char>(std::clamp(static_cast<int32_t>(255 * color.x), 0, 255)),
                    static_cast<char>(std::clamp(static_cast<int32_t>(255 * color.y), 0, 255)),
                    static_cast<char>(std::clamp(static_cast<int32_t>(255 * color.z), 0, 255)),
                    static_cast<char>(std::clamp(static_cast<int32_t>(255 * color.w), 0, 255))},
        1,
        1);
}

Texture::Texture(const Context& context, std::span<const char> data, size_t width, size_t height)
    : _context {context}
{
    load(data, width, height);
}

auto Texture::load(std::span<const char> data, size_t width, size_t height) -> void
{
    const auto imageSize = width * height * 4;
    auto stagingBuffer = Buffer {_context.getDevice(),
                                 static_cast<vk::DeviceSize>(imageSize),
                                 vk::BufferUsageFlagBits::eTransferSrc,
                                 vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent};

    stagingBuffer.mapWhole();
    stagingBuffer.write(data.data(), imageSize);
    stagingBuffer.unmapWhole();

    const auto imageInfo = vk::ImageCreateInfo {
        {},
        vk::ImageType::e2D,
        vk::Format::eR8G8B8A8Srgb,
        {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1},
        1,
        1,
        vk::SampleCountFlagBits::e1,
        vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
        vk::SharingMode::eExclusive,
        {},
        {},
        vk::ImageLayout::eUndefined
    };

    _image = expect(_context.getDevice().logicalDevice.createImage(imageInfo),
                    vk::Result::eSuccess,
                    "Failed to create image");

    const auto memoryRequirements = _context.getDevice().logicalDevice.getImageMemoryRequirements(_image);

    const auto allocationInfo = vk::MemoryAllocateInfo {
        memoryRequirements.size,
        expect(_context.getDevice().findMemoryType(memoryRequirements.memoryTypeBits,
                                                   vk::MemoryPropertyFlagBits::eDeviceLocal),
               "Can't find device local memory"),
    };

    _imageMemory = expect(_context.getDevice().logicalDevice.allocateMemory(allocationInfo),
                          vk::Result::eSuccess,
                          "Failed to allocate memory");
    expect(_context.getDevice().logicalDevice.bindImageMemory(_image, _imageMemory, 0),
           vk::Result::eSuccess,
           "Failed to bind image memory");

    transitionImageLayout(_context.getDevice(),
                          _image,
                          vk::ImageLayout::eUndefined,
                          vk::ImageLayout::eTransferDstOptimal);

    copyBufferToImage(_context.getDevice(), stagingBuffer, _image, width, height);
    transitionImageLayout(_context.getDevice(),
                          _image,
                          vk::ImageLayout::eTransferDstOptimal,
                          vk::ImageLayout::eShaderReadOnlyOptimal);

    _imageView = createTextureImageView(_context.getDevice(), _image);
    _sampler = createTextureSampler(_context.getDevice());
}
}