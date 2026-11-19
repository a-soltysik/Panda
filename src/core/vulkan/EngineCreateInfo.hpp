#pragma once

#include <cstdint>
#include <memory>
#include <panda/Engine.hpp>
#include <panda/rendering/TextureFilteringPreset.hpp>

#include "DeviceContext.hpp"
#include "SubmissionQueue.hpp"
#include "external/ExternalBufferInterop.hpp"
#include "performance/PerformanceManagerImpl.hpp"
#include "presentation/OffscreenRenderBackend.hpp"
#include "presentation/WindowPresentation.hpp"
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
    std::unique_ptr<presentation::OffscreenRenderBackend> offscreenRenderBackend;
    std::unique_ptr<WindowPresentation> windowPresentation;
    std::shared_ptr<TextureFilteringPreset> textureFilteringPreset {
        std::make_shared<TextureFilteringPreset>(TextureFilteringPreset::Anisotropic8x)};
    std::uint32_t maxLights {};
    EngineFeatures features;
};
}
