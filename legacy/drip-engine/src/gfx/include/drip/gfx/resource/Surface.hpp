#pragma once

#include "Mesh.hpp"
#include "Texture.hpp"

namespace drip::gfx
{

class Surface
{
public:
    Surface(const Texture* texture, const Mesh* mesh);

    [[nodiscard]] auto getTexture() const noexcept -> const Texture&;
    [[nodiscard]] auto getMesh() const noexcept -> const Mesh&;

    constexpr auto operator<=>(const Surface&) const noexcept = default;

private:
    const Texture* _texture;
    const Mesh* _mesh;
};

}
