#pragma once

#include <driver_types.h>

#include <cstddef>

#include "drip/simulation/ExternalMemory.cuh"

namespace drip::sim
{

class CudaExternalMemory : public ExternalMemory
{
public:
    CudaExternalMemory(Handle handle, size_t size);
    CudaExternalMemory(const CudaExternalMemory&) = delete;
    CudaExternalMemory(CudaExternalMemory&&) = delete;
    auto operator=(const CudaExternalMemory&) = delete;
    auto operator=(CudaExternalMemory&&) = delete;

    ~CudaExternalMemory() noexcept override;

    [[nodiscard]] auto getData() const -> void* override;
    [[nodiscard]] auto getSize() const -> size_t override;

private:
    cudaExternalMemory_t _memoryHandle {};
    void* _data {};
    size_t _size;
};

}
