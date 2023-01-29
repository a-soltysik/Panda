#pragma once

#include <string_view>

namespace panda::gfx::vk
{

class ValidationLayersHandler
{
public:
    void add(std::string_view validationLayer);
    [[nodiscard]] auto areValidationLayersSupported() const -> bool;
    [[nodiscard]] auto getData() const noexcept -> const char* const*;
    [[nodiscard]] auto getCount() const noexcept -> size_t;

private:
    std::vector<const char*> validationLayers;
};

}
