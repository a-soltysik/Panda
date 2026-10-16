#pragma once

#include <cstdint>

namespace panda::core::vulkan::upload
{
class UploadBatchId final
{
public:
    explicit UploadBatchId(std::uint64_t value)
        : _value {value}
    {
    }

    auto operator==(const UploadBatchId&) const -> bool = default;

private:
    std::uint64_t _value;
};
}
