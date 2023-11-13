#pragma once

#include "Mesh.h"

namespace panda::gfx::vulkan
{

struct Surface
{
    Texture* texture;
    Mesh* mesh;
};

}