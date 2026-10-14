panda_add_component(panda_core
    ALIAS Panda::Panda
    INCLUDE_DIRECTORY "${PROJECT_SOURCE_DIR}/include/core"
    SOURCES
        Panda.cpp
        Error.cpp
        Buffer.cpp
        Texture.cpp
        CompletionPoint.cpp
        UploadBatch.cpp
        vulkan/Context.cpp
        vulkan/DeviceSelection.cpp
        vulkan/Presentation.cpp
        vulkan/SubmissionQueue.cpp
        vulkan/UploadBatchImpl.cpp
        vulkan/UploadManager.cpp
        vulkan/UploadCommand.cpp
        vulkan/SwapchainGeneration.cpp
        vulkan/VulkanError.cpp
        vulkan/resources/RangeAllocator.cpp
        vulkan/resources/MemoryBlock.cpp
        vulkan/resources/MemoryAllocation.cpp
        vulkan/resources/MemoryPool.cpp
        vulkan/resources/BufferResource.cpp
        vulkan/resources/TextureResource.cpp
    LIBRARIES PUBLIC panda_common glm::glm Vulkan::Headers PRIVATE Vulkan::Vulkan)

set_target_properties(panda_core PROPERTIES OUTPUT_NAME panda)
target_include_directories(panda_core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/vulkan")
target_compile_definitions(panda_core PRIVATE PANDA_VERSION_TEXT="${PROJECT_VERSION}"
    VULKAN_HPP_NO_EXCEPTIONS VULKAN_HPP_NO_CONSTRUCTORS)
