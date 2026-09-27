#include "drip/gfx/resource/MeshRenderable.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "drip/gfx/resource/Surface.hpp"

namespace drip::gfx
{

MeshRenderable::MeshRenderable(std::string name)
    : _name {std::move(name)}
{
}

auto MeshRenderable::getName() const -> std::string_view
{
    return _name;
}

auto MeshRenderable::getType() const -> Type
{
    return Type::Mesh;
}

void MeshRenderable::addSurface(const Surface& surface)
{
    surfaces.push_back(surface);
}

auto MeshRenderable::getSurfaces() const noexcept -> const std::vector<Surface>&
{
    return surfaces;
}

}
