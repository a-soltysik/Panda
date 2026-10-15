#pragma once

#include <cstdint>

namespace panda::detail
{
class UploadBatchId final
{
public:
    friend auto operator==(UploadBatchId, UploadBatchId) -> bool = default;

private:
    friend class UploadManager;

    explicit UploadBatchId(std::uint64_t value)
        : _value {value}
    {
    }

    std::uint64_t _value;
};
}
