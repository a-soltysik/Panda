panda_add_component(panda_core
    ALIAS Panda::Panda
    INCLUDE_DIRECTORY "${PROJECT_SOURCE_DIR}/include/core"
    SOURCES
        Panda.cpp
        Camera.cpp
        Math.cpp
        Projection.cpp
        Transform.cpp
        geometry/Cube.cpp
        geometry/Plane.cpp
        geometry/Sphere.cpp
        Buffer.cpp
        Mesh.cpp
        Texture.cpp
        OffscreenTarget.cpp
        ResourceManager.cpp
        OffscreenRenderer.cpp
        SubmissionManager.cpp
        WindowRenderer.cpp
        vulkan/external/ExternalBuffer.cpp
        vulkan/external/ExternalBufferManager.cpp
        vulkan/external/ExternalBufferOwnershipTransferCommand.cpp
        vulkan/external/ExternalMemoryHandle.cpp
        vulkan/external/ExternalSemaphoreHandle.cpp
        vulkan/external/ExternalSemaphoreResource.cpp
        CompletionPoint.cpp
        UploadBatch.cpp
        vulkan/Engine.cpp
        vulkan/EngineCreation.cpp
        vulkan/InstanceCreation.cpp
        vulkan/DeviceSelection.cpp
        vulkan/ResourceMappings.cpp
        vulkan/external/ExternalBufferInterop.cpp
        vulkan/performance/PerformanceManager.cpp
        vulkan/performance/PerformanceManagerImpl.cpp
        vulkan/presentation/FrameExecution.cpp
        vulkan/presentation/OffscreenRenderBackend.cpp
        vulkan/presentation/WindowPresentation.cpp
        vulkan/renderer/FrameBindings.cpp
        vulkan/renderer/MeshRenderer.cpp
        vulkan/renderer/MeshPipelineCache.cpp
        vulkan/renderer/RenderAttachments.cpp
        vulkan/renderer/RenderViewValidation.cpp
        vulkan/renderer/ShadowAtlas.cpp
        vulkan/renderer/ShadowView.cpp
        vulkan/SubmissionQueue.cpp
        vulkan/upload/UploadBatchImpl.cpp
        vulkan/upload/UploadManager.cpp
        vulkan/upload/UploadCommand.cpp
        vulkan/presentation/SwapchainGeneration.cpp
        vulkan/VulkanError.cpp
        vulkan/resources/memory/RangeAllocator.cpp
        vulkan/resources/memory/MemoryBlock.cpp
        vulkan/resources/memory/MemoryAllocation.cpp
        vulkan/resources/memory/MemoryPool.cpp
        vulkan/resources/BufferResource.cpp
        vulkan/resources/MeshLayout.cpp
        vulkan/resources/MeshResource.cpp
        vulkan/resources/TextureResource.cpp
        vulkan/resources/OffscreenTargetResource.cpp
    PUBLIC_HEADERS
        panda/resources/Buffer.hpp
        panda/rendering/Camera.hpp
        panda/submission/CompletionPoint.hpp
        panda/Engine.hpp
        panda/DeviceInfo.hpp
        panda/EngineOptions.hpp
        panda/geometry/Cube.hpp
        panda/geometry/Geometry.hpp
        panda/geometry/Plane.hpp
        panda/geometry/Sphere.hpp
        panda/geometry/VertexLayout.hpp
        panda/math/Projection.hpp
        panda/math/Transform.hpp
        panda/rendering/FrameResult.hpp
        panda/interop/ExternalBuffer.hpp
        panda/interop/ExternalBufferManager.hpp
        panda/interop/ExternalMemoryHandle.hpp
        panda/interop/ExternalSemaphoreHandle.hpp
        panda/interop/ExternalTimelineValue.hpp
        panda/DeviceUuid.hpp
        panda/resources/Mesh.hpp
        panda/Panda.hpp
        panda/rendering/PerformanceManager.hpp
        panda/resources/PixelFormat.hpp
        panda/rendering/OffscreenRenderer.hpp
        panda/resources/ResourceManager.hpp
        panda/submission/SubmissionManager.hpp
        panda/resources/Texture.hpp
        panda/resources/OffscreenTarget.hpp
        panda/submission/UploadBatch.hpp
        panda/vulkan/WindowSurface.hpp
        panda/vulkan/Error.hpp
        panda/rendering/WindowRenderer.hpp
        panda/rendering/RenderView.hpp
    PRIVATE_HEADERS
        Math.hpp
        vulkan/EngineCreation.hpp
        vulkan/EngineCreateInfo.hpp
        vulkan/DeviceContext.hpp
        vulkan/DeviceSelection.hpp
        vulkan/ResourceMappings.hpp
        vulkan/InstanceResources.hpp
        vulkan/InstanceCreation.hpp
        vulkan/external/ExternalBufferInterop.hpp
        vulkan/external/ExternalSemaphoreResource.hpp
        vulkan/external/ExternalBufferOwnershipTransferCommand.hpp
        vulkan/external/ScopedExternalHandle.hpp
        vulkan/performance/PerformanceManagerImpl.hpp
        vulkan/presentation/WindowPresentation.hpp
        vulkan/presentation/FrameExecution.hpp
        vulkan/presentation/OffscreenRenderBackend.hpp
        vulkan/SubmissionQueue.hpp
        vulkan/presentation/SwapchainGeneration.hpp
        vulkan/upload/UploadBatchId.hpp
        vulkan/upload/UploadBatchImpl.hpp
        vulkan/upload/UploadCommand.hpp
        vulkan/upload/UploadManager.hpp
        vulkan/upload/UploadOperation.hpp
        vulkan/VulkanError.hpp
        vulkan/VulkanHpp.hpp
        vulkan/resources/BufferResource.hpp
        vulkan/resources/memory/MemoryAllocation.hpp
        vulkan/resources/memory/MemoryBlock.hpp
        vulkan/resources/memory/MemoryPool.hpp
        vulkan/resources/memory/MemoryProperties.hpp
        vulkan/resources/MeshLayout.hpp
        vulkan/resources/MeshResource.hpp
        vulkan/resources/memory/RangeAllocator.hpp
        vulkan/resources/TextureResource.hpp
        vulkan/resources/OffscreenTargetResource.hpp
        vulkan/renderer/MeshRenderer.hpp
        vulkan/renderer/FrameBindings.hpp
        vulkan/renderer/MaterialTextureSlots.hpp
        vulkan/renderer/MeshPipelineCache.hpp
        vulkan/renderer/RenderAttachments.hpp
        vulkan/renderer/ShadowAtlas.hpp
        vulkan/renderer/ShaderData.hpp
        vulkan/renderer/RenderViewValidation.hpp
        vulkan/renderer/ShadowView.hpp
    LIBRARIES PUBLIC panda_base Vulkan::Headers glm::glm
             PRIVATE Vulkan::Vulkan)
target_include_directories(panda_core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
add_library(panda_core_internal INTERFACE)
set_target_properties(panda_core_internal PROPERTIES FOLDER "Panda/Internal")
target_include_directories(panda_core_internal INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(panda_core PRIVATE panda_base_internal)
panda_add_builtin_shaders(panda_core)

if(PANDA_BUILD_CUDA)
    # Supply DeviceUuid headers without Core's C++23 requirement to the CUDA 20 adapter.
    add_library(panda_core_cuda_adapter_api INTERFACE)
    set_target_properties(panda_core_cuda_adapter_api PROPERTIES FOLDER "Panda/Internal")
    target_include_directories(panda_core_cuda_adapter_api INTERFACE "${PROJECT_SOURCE_DIR}/include/core")
    target_link_libraries(panda_core_cuda_adapter_api INTERFACE Vulkan::Headers)
endif()

set_target_properties(panda_core PROPERTIES OUTPUT_NAME panda)
target_compile_definitions(panda_core PRIVATE PANDA_VERSION_TEXT="${PROJECT_VERSION}")
panda_configure_vulkan_internal(panda_core)
