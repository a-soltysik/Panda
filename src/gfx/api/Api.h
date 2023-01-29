#pragma once

namespace panda::gfx
{

class Api
{
public:
    virtual ~Api() noexcept = default;

    virtual auto init() -> bool = 0;
    virtual auto cleanup() -> void = 0;
};

}
