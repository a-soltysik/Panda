#pragma once
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

#include "Renderable.hpp"
#include "drip/gfx/vulkan/core/Context.hpp"

namespace drip::gfx
{

class SharedBuffer;

class ParticlesRenderable : public Renderable
{
public:
    struct DataBuffer
    {
#if defined(WIN32)
        using Handle = void*;
#else
        using Handle = int;
#endif
        Handle translations;  // glm::vec4
        Handle colors;        // glm::vec4
        Handle sizes;         // float
    };

    struct VulkanDataBuffer
    {
        std::unique_ptr<SharedBuffer> translations;  // glm::vec4
        std::unique_ptr<SharedBuffer> colors;        // glm::vec4
        std::unique_ptr<SharedBuffer> sizes;         // float
    };

    ParticlesRenderable(const Context& context, std::string name, size_t size);
    ParticlesRenderable(const ParticlesRenderable&) = delete;
    ParticlesRenderable(ParticlesRenderable&&) = delete;
    auto operator=(const ParticlesRenderable&) = delete;
    auto operator=(ParticlesRenderable&&) = delete;
    ~ParticlesRenderable() noexcept override;

    [[nodiscard]] auto getName() const -> std::string_view override;
    [[nodiscard]] auto getType() const -> Type override;
    [[nodiscard]] auto getDataBuffer() const -> DataBuffer;
    [[nodiscard]] auto getVulkanDataBuffer() const -> const VulkanDataBuffer&;
    [[nodiscard]] auto getSize() const -> size_t;

private:
    VulkanDataBuffer _vulkanDataBuffer;
    DataBuffer _dataBuffer;
    std::string _name;
    size_t _size;
};

}
