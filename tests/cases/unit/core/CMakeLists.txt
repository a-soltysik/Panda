panda_add_test(panda_unit_range_allocator unit
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/core/vulkan"
    SOURCES RangeAllocator.cpp
    LIBRARIES Panda::Panda)

panda_add_test(panda_unit_memory_block unit
    VULKAN_INTERNAL
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/core/vulkan/resources"
    SOURCES MemoryBlock.cpp
    LIBRARIES Panda::Panda PandaTest::Support
    MOCKS PandaTest::VulkanMock PandaTest::RangeAllocatorMock)

panda_add_test(panda_unit_memory_pool unit
    VULKAN_INTERNAL
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/core/vulkan/resources"
    SOURCES MemoryPool.cpp
    LIBRARIES Panda::Panda PandaTest::Support
    MOCKS PandaTest::VulkanMock PandaTest::MemoryBlockMock)

panda_add_test(panda_unit_buffer_resource unit
    VULKAN_INTERNAL
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/core/vulkan/resources"
    SOURCES BufferResource.cpp
    LIBRARIES Panda::Panda PandaTest::Support
    MOCKS PandaTest::VulkanMock PandaTest::MemoryPoolMock PandaTest::MemoryAllocationMock)

panda_add_test(panda_unit_texture_resource unit
    VULKAN_INTERNAL
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/core/vulkan/resources"
    SOURCES TextureResource.cpp
    LIBRARIES Panda::Panda PandaTest::Support
    MOCKS PandaTest::VulkanMock PandaTest::MemoryPoolMock PandaTest::MemoryAllocationMock)

panda_add_test(panda_unit_mesh_resource unit
    VULKAN_INTERNAL
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/core/vulkan/resources"
    SOURCES MeshResource.cpp
    LIBRARIES Panda::Panda PandaTest::Support
    MOCKS PandaTest::VulkanMock PandaTest::MemoryPoolMock PandaTest::MemoryAllocationMock
          PandaTest::BufferResourceMock PandaTest::MeshLayoutMock)

panda_add_test(panda_unit_mesh_layout unit
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/core/vulkan/resources"
    SOURCES MeshLayout.cpp
    LIBRARIES Panda::Panda)

panda_add_test(panda_unit_vulkan_error unit
    SOURCES VulkanError.cpp
    LIBRARIES Panda::Panda)

panda_add_test(panda_unit_core unit
    VULKAN_INTERNAL
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/core"
    SOURCES Version.cpp DeviceSelection.cpp SwapchainGeneration.cpp OffscreenRendering.cpp
            SwapchainCreation.cpp ExternalTimelineValue.cpp RenderViewValidation.cpp ShadowAtlas.cpp ShadowView.cpp
    LIBRARIES Panda::Panda PandaTest::Support PandaTest::LogCapture
    MOCKS PandaTest::VulkanMock PandaTest::SubmissionQueueMock)

panda_add_test(panda_unit_math unit
    SOURCES Math.cpp Projection.cpp Transform.cpp
    LIBRARIES Panda::Panda panda_core_internal)

panda_add_test(panda_unit_cube unit
    SOURCES Cube.cpp
    LIBRARIES Panda::Panda)

panda_add_test(panda_unit_plane unit
    SOURCES Plane.cpp
    LIBRARIES Panda::Panda)

panda_add_test(panda_unit_sphere unit
    SOURCES Sphere.cpp
    LIBRARIES Panda::Panda)
target_compile_definitions(panda_unit_core PRIVATE
    PANDA_EXPECTED_VERSION="${PROJECT_VERSION}")

panda_add_test(panda_unit_camera unit
    SOURCES Camera.cpp
    LIBRARIES Panda::Panda)

panda_add_test(panda_unit_engine unit
    VULKAN_INTERNAL
    SOURCES Engine.cpp EngineLifecycle.cpp
    HEADERS "${PROJECT_SOURCE_DIR}/tests/support/VulkanTestSupport.hpp"
            "${PROJECT_SOURCE_DIR}/tests/support/mocks/panda/core/WindowSurfaceMock.hpp"
    LIBRARIES Panda::Panda PandaTest::Support PandaTest::LogCapture
    MOCKS PandaTest::VulkanMock PandaTest::SwapchainGenerationMock)

panda_add_test(panda_unit_external_buffer_interop unit
    VULKAN_INTERNAL
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/core/vulkan/resources"
    SOURCES ExternalBufferInterop.cpp
    LIBRARIES Panda::Panda PandaTest::Support
    MOCKS PandaTest::VulkanMock PandaTest::BufferResourceMock PandaTest::SubmissionQueueMock)

panda_add_test(panda_unit_upload_batch unit
    SOURCES UploadBatch.cpp
    LIBRARIES Panda::Panda)

panda_add_test(panda_unit_environment_map unit
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/core"
    SOURCES EnvironmentMap.cpp
    LIBRARIES Panda::Panda)

panda_add_test(panda_unit_upload_batch_impl unit
    VULKAN_INTERNAL
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/core/vulkan/resources"
                       "${PROJECT_SOURCE_DIR}/src/core/vulkan/upload"
    SOURCES UploadBatchImpl.cpp TextureMipGeneration.cpp
    LIBRARIES Panda::Panda PandaTest::Support
    MOCKS PandaTest::VulkanMock PandaTest::MemoryPoolMock PandaTest::MemoryAllocationMock
          PandaTest::SubmissionQueueMock
          PandaTest::UploadManagerMock PandaTest::BufferResourceMock PandaTest::TextureResourceMock
          PandaTest::MeshResourceMock PandaTest::MeshLayoutMock)

panda_add_test(panda_unit_upload_command unit
    VULKAN_INTERNAL
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/core/vulkan/resources"
    SOURCES UploadCommand.cpp
    LIBRARIES Panda::Panda PandaTest::Support
    MOCKS PandaTest::VulkanMock PandaTest::MemoryPoolMock PandaTest::MemoryAllocationMock
          PandaTest::BufferResourceMock PandaTest::TextureResourceMock)

panda_add_test(panda_unit_mesh unit
    SOURCES Mesh.cpp
    LIBRARIES Panda::Panda)

panda_add_test(panda_unit_upload_manager unit
    VULKAN_INTERNAL
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/core/vulkan/resources"
    SOURCES UploadManager.cpp
    LIBRARIES Panda::Panda PandaTest::Support
    MOCKS PandaTest::VulkanMock PandaTest::MemoryPoolMock PandaTest::MemoryAllocationMock
          PandaTest::BufferResourceMock PandaTest::TextureResourceMock PandaTest::UploadCommandMock
          PandaTest::SubmissionQueueMock)

panda_add_test(panda_unit_submission_queue unit
    VULKAN_INTERNAL
    SOURCES SubmissionQueue.cpp
    LIBRARIES Panda::Panda PandaTest::Support
    MOCKS PandaTest::VulkanMock)

panda_add_test(panda_unit_performance_manager unit
    VULKAN_INTERNAL
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/core/vulkan/performance"
    SOURCES PerformanceManager.cpp
    LIBRARIES Panda::Panda PandaTest::Support
    MOCKS PandaTest::VulkanMock PandaTest::SubmissionQueueMock)

panda_add_test(panda_unit_presentation unit
    VULKAN_INTERNAL
    SOURCES Presentation.cpp
    HEADERS "${PROJECT_SOURCE_DIR}/tests/support/PresentationTestSupport.hpp"
            "${PROJECT_SOURCE_DIR}/tests/support/VulkanTestSupport.hpp"
            "${PROJECT_SOURCE_DIR}/tests/support/mocks/panda/core/WindowSurfaceMock.hpp"
    LIBRARIES Panda::Panda PandaTest::Support
    MOCKS PandaTest::VulkanMock PandaTest::SwapchainGenerationMock PandaTest::SubmissionQueueMock)

panda_add_test(panda_unit_mesh_pipeline_cache unit
    VULKAN_INTERNAL
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/core"
    SOURCES MeshPipelineCache.cpp
    LIBRARIES Panda::Panda PandaTest::Support
    MOCKS PandaTest::VulkanMock)

panda_add_test(panda_unit_frame_bindings unit
    VULKAN_INTERNAL
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/core" "${PROJECT_SOURCE_DIR}/src/core/vulkan/resources"
    SOURCES FrameBindings.cpp
    LIBRARIES Panda::Panda PandaTest::Support
    MOCKS PandaTest::VulkanMock PandaTest::BufferResourceMock
          PandaTest::TextureResourceMock PandaTest::SubmissionQueueMock)

panda_add_test(panda_unit_render_attachments unit
    VULKAN_INTERNAL
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/core" "${PROJECT_SOURCE_DIR}/src/core/vulkan/resources"
    SOURCES RenderAttachments.cpp
    LIBRARIES Panda::Panda PandaTest::Support
    MOCKS PandaTest::VulkanMock PandaTest::TextureResourceMock)

panda_add_test(panda_unit_shader_reflection unit
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/core" "${PROJECT_SOURCE_DIR}/src/core/vulkan"
    SOURCES ShaderReflection.cpp "${PROJECT_SOURCE_DIR}/src/core/vulkan/renderer/ShaderReflection.cpp"
    LIBRARIES panda_base
    MOCKS PandaTest::SpirvReflectMock)
