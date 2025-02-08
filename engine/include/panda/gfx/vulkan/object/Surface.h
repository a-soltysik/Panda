#pragma once

#include <cstddef>
#include <functional>

#include "Mesh.h"
#include "Texture.h"
#include "panda/utils/Utils.h"

namespace panda::gfx::vulkan
{

class Surface
{
public:
    Surface(const Texture* texture, const Mesh* mesh, bool isInstanced = false);

    [[nodiscard]] auto getTexture() const noexcept -> const Texture&;
    [[nodiscard]] auto getMesh() const noexcept -> const Mesh&;
    [[nodiscard]] auto isInstanced() const noexcept -> bool;

    constexpr auto operator<=>(const Surface&) const noexcept = default;

private:
    const Texture* _texture;
    const Mesh* _mesh;
    bool _isInstanced = false;
};

}

template <>
struct std::hash<panda::gfx::vulkan::Surface>
{
    auto operator()(const panda::gfx::vulkan::Surface& surface) const noexcept -> size_t
    {
        auto seed = size_t {};
        panda::utils::hashCombine(seed, &surface.getTexture(), &surface.getMesh(), surface.isInstanced());
        return seed;
    }
};
