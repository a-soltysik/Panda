panda_add_test(panda_unit_range_allocator unit
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/core/vulkan"
    SOURCES RangeAllocator.cpp
    LIBRARIES Panda::Panda)

panda_add_test(panda_unit_projection unit
    SOURCES Projection.cpp
    LIBRARIES Panda::Panda)

panda_add_test(panda_unit_cube unit
    SOURCES Cube.cpp
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

panda_add_test(panda_unit_core unit
    VULKAN_INTERNAL
    SOURCES Version.cpp DeviceSelection.cpp SwapchainGeneration.cpp
            SwapchainCreation.cpp ExternalTimelineValue.cpp RenderViewValidation.cpp
    LIBRARIES Panda::Panda PandaTest::Support PandaTest::LogCapture
    MOCKS PandaTest::VulkanMock)
target_compile_definitions(panda_unit_core PRIVATE
    PANDA_EXPECTED_VERSION="${PROJECT_VERSION}")

panda_add_test(panda_unit_context unit
    VULKAN_INTERNAL
    SOURCES Context.cpp ContextLifecycle.cpp
    LIBRARIES Panda::Panda PandaTest::Support PandaTest::LogCapture
    MOCKS PandaTest::VulkanMock PandaTest::SwapchainGenerationMock)

panda_add_test(panda_unit_external_context unit
    VULKAN_INTERNAL
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/core/vulkan/resources"
    SOURCES ExternalContext.cpp
    LIBRARIES Panda::Panda PandaTest::Support
    MOCKS PandaTest::VulkanMock PandaTest::BufferResourceMock PandaTest::SubmissionQueueMock)

panda_add_test(panda_unit_error unit
    SOURCES Error.cpp
    LIBRARIES Panda::Panda)

panda_add_test(panda_unit_upload_batch unit
    SOURCES UploadBatch.cpp
    LIBRARIES Panda::Panda)

panda_add_test(panda_unit_upload_batch_impl unit
    VULKAN_INTERNAL
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/core/vulkan/resources"
    SOURCES UploadBatchImpl.cpp
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
    LIBRARIES Panda::Panda PandaTest::Support
    MOCKS PandaTest::VulkanMock PandaTest::SwapchainGenerationMock PandaTest::SubmissionQueueMock)
