#include "drip/gfx/resource/Surface.hpp"

#include "drip/gfx/resource/Mesh.hpp"
#include "drip/gfx/resource/Texture.hpp"

namespace drip::gfx
{

Surface::Surface(const Texture* texture, const Mesh* mesh)
    : _texture {texture},
      _mesh {mesh}
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

}
