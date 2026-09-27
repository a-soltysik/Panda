#pragma once

namespace drip::gfx
{

class Renderable
{
public:
    enum class Type
    {
        Mesh,
        Particles,
    };

    Renderable() = default;
    Renderable(const Renderable&) = delete;
    Renderable(Renderable&&) = delete;
    auto operator=(const Renderable&) = delete;
    auto operator=(Renderable&&) = delete;
    virtual ~Renderable() noexcept = default;

    [[nodiscard]] virtual auto getType() const -> Type = 0;
    [[nodiscard]] virtual auto getName() const -> std::string_view = 0;
};

}
