#include <memory>
#include <panda/rendering/ShaderResources.hpp>
#include <span>
#include <utility>
#include <vector>

namespace panda
{
ShaderResources::ShaderResources(std::vector<ShaderResourceBinding> bindings)
    : _bindings {std::make_shared<const std::vector<ShaderResourceBinding>>(std::move(bindings))}
{
}

auto ShaderResources::getBindings() const noexcept -> std::span<const ShaderResourceBinding>
{
    return _bindings ? std::span<const ShaderResourceBinding> {*_bindings} : std::span<const ShaderResourceBinding> {};
}
}
