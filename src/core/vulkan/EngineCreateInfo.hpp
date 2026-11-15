#pragma once

#include <cstdint>
#include <memory>

#include "DeviceContext.hpp"
#include "SubmissionQueue.hpp"
#include "external/ExternalBufferInterop.hpp"
#include "performance/PerformanceManagerImpl.hpp"
#include "presentation/OffscreenRendering.hpp"
#include "presentation/Presentation.hpp"
#include "resources/memory/MemoryPool.hpp"
#include "upload/UploadManager.hpp"

namespace panda::core::vulkan
{
struct EngineCreateInfo
{
    std::unique_ptr<DeviceContext> device;
    std::unique_ptr<resources::MemoryPool> memoryPool;
    std::unique_ptr<detail::SubmissionQueue> submissions;
    std::unique_ptr<upload::UploadManager> uploads;
    std::unique_ptr<detail::ExternalBufferInterop> externalInterop;
    std::unique_ptr<detail::PerformanceManagerImpl> performance;
    std::unique_ptr<presentation::OffscreenRendering> offscreenRendering;
    std::unique_ptr<Presentation> presentation;
    std::uint32_t maxLights {};
};
}
