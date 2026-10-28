#include <cstdint>
#include <expected>
#include <memory>
#include <panda/Buffer.hpp>
#include <panda/Error.hpp>
#include <panda/ExternalTimelineValue.hpp>
#include <panda/cuda/SharedCudaBuffer.hpp>
#include <utility>

#include "SharedCudaBufferState.hpp"

namespace panda::cuda
{
SharedCudaBuffer::SharedCudaBuffer() = default;

SharedCudaBuffer::SharedCudaBuffer(const SharedCudaBuffer&) = default;

auto SharedCudaBuffer::operator=(const SharedCudaBuffer&) -> SharedCudaBuffer& = default;

SharedCudaBuffer::SharedCudaBuffer(SharedCudaBuffer&&) noexcept = default;

auto SharedCudaBuffer::operator=(SharedCudaBuffer&&) noexcept -> SharedCudaBuffer& = default;

SharedCudaBuffer::~SharedCudaBuffer() = default;

SharedCudaBuffer::SharedCudaBuffer(std::shared_ptr<detail::SharedCudaBufferState> state)
    : _state {std::move(state)}
{
}

auto SharedCudaBuffer::getBuffer() const -> panda::Buffer
{
    return _state != nullptr ? _state->getBuffer() : panda::Buffer {};
}

auto SharedCudaBuffer::getSize() const noexcept -> std::uint64_t
{
    return _state != nullptr ? _state->getSize() : 0;
}

auto SharedCudaBuffer::isValid() const noexcept -> bool
{
    return _state != nullptr;
}

auto SharedCudaBuffer::submitWork(panda::ExternalTimelineValue vulkanSignalValue, const CudaWorkCallback& work)
    -> panda::Result<CudaWorkSubmission>
{
    auto state = _state;
    if (state == nullptr)
    {
        return std::unexpected {panda::makeError(panda::ErrorCode::InvalidArgument, "Shared CUDA buffer is empty")};
    }
    return state->submitWork(vulkanSignalValue.getValue(), work);
}
}
