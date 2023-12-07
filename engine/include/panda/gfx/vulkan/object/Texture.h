#pragma once

#include <filesystem>

#include "panda/gfx/vulkan/Descriptor.h"

namespace panda::gfx::vulkan
{

class Context;

class Texture
{
public:
    [[nodiscard]] static auto getDefaultTexture(const Context& context, glm::vec4 color = {1.F, 1.F, 1.F, 1.F})
        -> std::unique_ptr<Texture>;
    [[nodiscard]] static auto fromFile(const Context& context, const std::filesystem::path& path)
        -> std::unique_ptr<Texture>;
    Texture(const Context& context, std::span<const char> data, size_t width, size_t height);

    ~Texture();

    [[nodiscard]] auto getDescriptorImageInfo() const noexcept -> vk::DescriptorImageInfo;

private:
    auto load(std::span<const char> data, size_t width, size_t height) -> void;

    const Context& _context;
    vk::Image _image;
    vk::ImageView _imageView;
    vk::DeviceMemory _imageMemory;
    vk::Sampler _sampler;
};

}