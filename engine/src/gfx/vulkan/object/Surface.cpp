#include "panda/gfx/vulkan/object/Surface.h"

#include "panda/gfx/vulkan/object/Mesh.h"
#include "panda/gfx/vulkan/object/Texture.h"

namespace panda::gfx::vulkan
{

Surface::Surface(const Texture* texture, const Mesh* mesh, bool isInstanced)
    : _texture {texture},
      _mesh {mesh},
      _isInstanced {isInstanced}
{
}

auto Surface::getTexture() const noexcept -> const Texture&
{
    return *_texture;
}

auto Surface::getMesh() const noexcept -> const Mesh&
{
    return *_mesh;
}

auto Surface::isInstanced() const noexcept -> bool
{
    return _isInstanced;
}
}
