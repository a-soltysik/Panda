#pragma once

namespace panda::gfx
{

class RenderingApi
{
public:
    RenderingApi() = default;
    RenderingApi(const RenderingApi&) = delete;
    RenderingApi(RenderingApi&&) = default;
    auto operator=(const RenderingApi&) -> RenderingApi& = delete;
    auto operator=(RenderingApi&&) -> RenderingApi& = default;

    virtual ~RenderingApi() noexcept = default;
    virtual auto render() -> void = 0;
};

}