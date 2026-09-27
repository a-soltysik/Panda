#include "drip/gfx/resource/ParticlesRenderable.hpp"

#include <cstddef>
#include <glm/ext/vector_float4.hpp>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "drip/gfx/vulkan/core/Context.hpp"
#include "vulkan/core/Device.hpp"
#include "vulkan/memory/SharedBuffer.hpp"
#include "vulkan/vulkan.hpp"

namespace drip::gfx
{

namespace
{
template <typename T>
auto createSharedBuffer(const Device& device, size_t size) -> std::unique_ptr<SharedBuffer>
{
    return std::make_unique<SharedBuffer>(device,
                                          sizeof(T),
                                          size,
                                          vk::MemoryPropertyFlagBits::eDeviceLocal,
                                          device.physicalDevice.getProperties().limits.minUniformBufferOffsetAlignment);
}
}

ParticlesRenderable::ParticlesRenderable(const Context& context, std::string name, size_t size)
    : _vulkanDataBuffer {.translations = createSharedBuffer<glm::vec4>(context.getDevice(), size),
                         .colors = createSharedBuffer<glm::vec4>(context.getDevice(), size),
                         .sizes = createSharedBuffer<float>(context.getDevice(), size)},
      _dataBuffer {.translations = _vulkanDataBuffer.translations->getBufferHandle(),
                   .colors = _vulkanDataBuffer.colors->getBufferHandle(),
                   .sizes = _vulkanDataBuffer.sizes->getBufferHandle()},
      _name(std::move(name)),
      _size(size)
{
}

ParticlesRenderable::~ParticlesRenderable() noexcept = default;

auto ParticlesRenderable::getName() const -> std::string_view
{
    return _name;
}

auto ParticlesRenderable::getType() const -> Type
{
    return Type::Particles;
}

auto ParticlesRenderable::getDataBuffer() const -> DataBuffer
{
    return _dataBuffer;
}

auto ParticlesRenderable::getVulkanDataBuffer() const -> const VulkanDataBuffer&
{
    return _vulkanDataBuffer;
}

auto ParticlesRenderable::getSize() const -> size_t
{
    return _size;
}

}
