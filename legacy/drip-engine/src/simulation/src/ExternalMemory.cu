#include "CudaExternalMemory.cuh"
#include "drip/simulation/ExternalMemory.cuh"

namespace drip::sim
{

auto ExternalMemory::create(Handle handle, size_t size) -> std::unique_ptr<ExternalMemory>
{
    auto result = std::make_unique<CudaExternalMemory>(handle, size);
    return result;
}

}
