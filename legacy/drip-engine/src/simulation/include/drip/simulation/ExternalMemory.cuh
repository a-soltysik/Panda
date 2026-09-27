#pragma once

#include <cstddef>
#include <memory>

namespace drip::sim
{

class ExternalMemory
{
public:
#if defined(_WIN32)
    using Handle = void*;
#else
    using Handle = int;
#endif

    static auto create(Handle handle, size_t size) -> std::unique_ptr<ExternalMemory>;

    ExternalMemory() = default;
    ExternalMemory(const ExternalMemory&) = delete;
    ExternalMemory(ExternalMemory&&) = delete;
    auto operator=(const ExternalMemory&) = delete;
    auto operator=(ExternalMemory&&) = delete;
    virtual ~ExternalMemory() noexcept = default;

    [[nodiscard]] virtual auto getData() const -> void* = 0;
    [[nodiscard]] virtual auto getSize() const -> size_t = 0;
};
}
