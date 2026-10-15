panda_add_test(panda_unit_range_allocator unit
    SOURCES RangeAllocator.cpp
    LIBRARIES Panda::Panda)
target_include_directories(panda_unit_range_allocator PRIVATE "${PROJECT_SOURCE_DIR}/src/core/vulkan/resources")

panda_add_test(panda_unit_memory_block unit
    SOURCES MemoryBlock.cpp
    LIBRARIES Panda::Panda PandaTest::Support
    MOCKS PandaTest::VulkanMock)
target_include_directories(panda_unit_memory_block PRIVATE "${PROJECT_SOURCE_DIR}/src/core/vulkan"
    "${PROJECT_SOURCE_DIR}/src/core/vulkan/resources")
target_compile_definitions(panda_unit_memory_block PRIVATE VULKAN_HPP_NO_CONSTRUCTORS VULKAN_HPP_NO_EXCEPTIONS)

panda_add_test(panda_unit_memory_pool unit
    SOURCES MemoryPool.cpp
    LIBRARIES Panda::Panda PandaTest::Support
    MOCKS PandaTest::VulkanMock)
target_include_directories(panda_unit_memory_pool PRIVATE "${PROJECT_SOURCE_DIR}/src/core/vulkan"
    "${PROJECT_SOURCE_DIR}/src/core/vulkan/resources")
target_compile_definitions(panda_unit_memory_pool PRIVATE VULKAN_HPP_NO_CONSTRUCTORS VULKAN_HPP_NO_EXCEPTIONS)

panda_add_test(panda_unit_texture_resource unit
    SOURCES TextureResource.cpp
    LIBRARIES Panda::Panda PandaTest::Support
    MOCKS PandaTest::VulkanMock)
target_include_directories(panda_unit_texture_resource PRIVATE "${PROJECT_SOURCE_DIR}/src/core/vulkan"
    "${PROJECT_SOURCE_DIR}/src/core/vulkan/resources")
target_compile_definitions(panda_unit_texture_resource PRIVATE VULKAN_HPP_NO_CONSTRUCTORS VULKAN_HPP_NO_EXCEPTIONS)

panda_add_test(panda_unit_mesh_layout unit
    SOURCES MeshLayout.cpp
    LIBRARIES Panda::Panda)
target_include_directories(panda_unit_mesh_layout PRIVATE "${PROJECT_SOURCE_DIR}/src/core/vulkan/resources")

panda_add_test(panda_unit_core unit
    SOURCES Version.cpp DeviceSelection.cpp SwapchainGeneration.cpp SwapchainCreation.cpp
    LIBRARIES Panda::Panda PandaTest::Support PandaTest::LogCapture
    MOCKS PandaTest::VulkanMock)
target_compile_definitions(panda_unit_core PRIVATE
    PANDA_EXPECTED_VERSION="${PROJECT_VERSION}"
    VULKAN_HPP_NO_CONSTRUCTORS
    VULKAN_HPP_NO_EXCEPTIONS)
target_include_directories(panda_unit_core PRIVATE "${PROJECT_SOURCE_DIR}/src/core/vulkan")

panda_add_test(panda_unit_context unit
    SOURCES Context.cpp
    LIBRARIES Panda::Panda PandaTest::Support PandaTest::LogCapture
    MOCKS PandaTest::VulkanMock PandaTest::SwapchainGenerationMock)
target_include_directories(panda_unit_context PRIVATE "${PROJECT_SOURCE_DIR}/src/core/vulkan")
target_compile_definitions(panda_unit_context PRIVATE
    VULKAN_HPP_NO_CONSTRUCTORS
    VULKAN_HPP_NO_EXCEPTIONS)

panda_add_test(panda_unit_error unit
    SOURCES Error.cpp
    LIBRARIES Panda::Panda)

panda_add_test(panda_unit_upload_batch unit
    SOURCES UploadBatch.cpp
    LIBRARIES Panda::Panda)

panda_add_test(panda_unit_upload_manager unit
    SOURCES UploadManager.cpp
    LIBRARIES Panda::Panda PandaTest::Support
    MOCKS PandaTest::VulkanMock PandaTest::SubmissionQueueMock)
target_include_directories(panda_unit_upload_manager PRIVATE "${PROJECT_SOURCE_DIR}/src/core/vulkan"
    "${PROJECT_SOURCE_DIR}/src/core/vulkan/resources")
target_compile_definitions(panda_unit_upload_manager PRIVATE VULKAN_HPP_NO_CONSTRUCTORS VULKAN_HPP_NO_EXCEPTIONS)

panda_add_test(panda_unit_submission_queue unit
    SOURCES SubmissionQueue.cpp
    LIBRARIES Panda::Panda PandaTest::Support
    MOCKS PandaTest::VulkanMock)
target_include_directories(panda_unit_submission_queue PRIVATE "${PROJECT_SOURCE_DIR}/src/core/vulkan")
target_compile_definitions(panda_unit_submission_queue PRIVATE VULKAN_HPP_NO_CONSTRUCTORS VULKAN_HPP_NO_EXCEPTIONS)

panda_add_test(panda_unit_presentation unit
    SOURCES Presentation.cpp
    LIBRARIES Panda::Panda PandaTest::Support
    MOCKS PandaTest::VulkanMock PandaTest::SwapchainGenerationMock PandaTest::SubmissionQueueMock)
target_include_directories(panda_unit_presentation PRIVATE "${PROJECT_SOURCE_DIR}/src/core/vulkan")
target_compile_definitions(panda_unit_presentation PRIVATE
    VULKAN_HPP_NO_CONSTRUCTORS
    VULKAN_HPP_NO_EXCEPTIONS)
