#pragma once

#include <cstdint>
#include <panda/base/StrongAlias.hpp>

namespace panda::core::vulkan::upload
{
struct UploadBatchIdTag;
using UploadBatchId = panda::StrongAlias<std::uint64_t, UploadBatchIdTag>;
}
