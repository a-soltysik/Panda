add_library(panda_test_mock_headers INTERFACE)
set_target_properties(panda_test_mock_headers PROPERTIES FOLDER "Panda/Test support")
add_library(PandaTest::MockHeaders ALIAS panda_test_mock_headers)
target_link_libraries(panda_test_mock_headers INTERFACE PandaTest::Support)
target_include_directories(panda_test_mock_headers INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")

panda_add_mock(panda_test_glfw_mock
    REPLACES glfw
    SOURCES external/glfw/GlfwStub.cpp
    HEADERS external/glfw/GlfwMock.hpp
    LIBRARIES PandaTest::MockHeaders)
add_library(PandaTest::GlfwMock ALIAS panda_test_glfw_mock)

# The unit boundary is Vulkan as a library, not an individual entry point.
# The stub provides the calls controlled by current Core unit scenarios.
panda_add_mock(panda_test_vulkan_mock
    REPLACES Vulkan::Vulkan
    API_LIBRARY Vulkan::Headers
    SOURCES external/vulkan/VulkanStub.cpp
    HEADERS external/vulkan/VulkanMock.hpp
    LIBRARIES PandaTest::MockHeaders)
add_library(PandaTest::VulkanMock ALIAS panda_test_vulkan_mock)

panda_add_mock(panda_test_range_allocator_mock
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/core/vulkan"
    REPLACES panda::core::vulkan::resources::RangeAllocator
    API_LIBRARY Panda::Panda
    SOURCES panda/core/RangeAllocatorStub.cpp
    HEADERS panda/core/RangeAllocatorMock.hpp
    LIBRARIES PandaTest::MockHeaders)
add_library(PandaTest::RangeAllocatorMock ALIAS panda_test_range_allocator_mock)

panda_add_mock(panda_test_memory_block_mock
    VULKAN_INTERNAL
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/core/vulkan/resources"
    REPLACES panda::core::vulkan::resources::MemoryBlock
    API_LIBRARY Panda::Panda
    SOURCES panda/core/MemoryBlockStub.cpp
    HEADERS panda/core/MemoryBlockMock.hpp
    LIBRARIES PandaTest::MockHeaders)
add_library(PandaTest::MemoryBlockMock ALIAS panda_test_memory_block_mock)

panda_add_mock(panda_test_memory_pool_mock
    VULKAN_INTERNAL
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/core/vulkan/resources"
    REPLACES panda::core::vulkan::resources::MemoryPool
    API_LIBRARY Panda::Panda
    SOURCES panda/core/MemoryPoolStub.cpp
    HEADERS panda/core/MemoryPoolMock.hpp
    LIBRARIES PandaTest::MockHeaders)
add_library(PandaTest::MemoryPoolMock ALIAS panda_test_memory_pool_mock)

panda_add_mock(panda_test_memory_allocation_mock
    VULKAN_INTERNAL
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/core/vulkan/resources"
    REPLACES panda::core::vulkan::resources::MemoryAllocation
    API_LIBRARY Panda::Panda
    SOURCES panda/core/MemoryAllocationStub.cpp
    HEADERS panda/core/MemoryAllocationMock.hpp
    LIBRARIES PandaTest::MockHeaders)
add_library(PandaTest::MemoryAllocationMock ALIAS panda_test_memory_allocation_mock)

panda_add_mock(panda_test_buffer_resource_mock
    VULKAN_INTERNAL
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/core/vulkan/resources"
    REPLACES panda::detail::BufferResource
    API_LIBRARY Panda::Panda
    SOURCES panda/core/BufferResourceStub.cpp
    HEADERS panda/core/BufferResourceMock.hpp
    LIBRARIES PandaTest::MockHeaders)
add_library(PandaTest::BufferResourceMock ALIAS panda_test_buffer_resource_mock)

panda_add_mock(panda_test_texture_resource_mock
    VULKAN_INTERNAL
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/core/vulkan/resources"
    REPLACES panda::detail::TextureResource
    API_LIBRARY Panda::Panda
    SOURCES panda/core/TextureResourceStub.cpp
    HEADERS panda/core/TextureResourceMock.hpp
    LIBRARIES PandaTest::MockHeaders)
add_library(PandaTest::TextureResourceMock ALIAS panda_test_texture_resource_mock)

panda_add_mock(panda_test_mesh_layout_mock
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/core/vulkan/resources"
    REPLACES panda::core::vulkan::resources::MeshLayout
    API_LIBRARY Panda::Panda
    SOURCES panda/core/MeshLayoutStub.cpp
    HEADERS panda/core/MeshLayoutMock.hpp
    LIBRARIES PandaTest::MockHeaders)
add_library(PandaTest::MeshLayoutMock ALIAS panda_test_mesh_layout_mock)

panda_add_mock(panda_test_mesh_resource_mock
    VULKAN_INTERNAL
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/core/vulkan/resources"
    REPLACES panda::detail::MeshResource
    API_LIBRARY Panda::Panda
    SOURCES panda/core/MeshResourceStub.cpp
    HEADERS panda/core/MeshResourceMock.hpp
    LIBRARIES PandaTest::MockHeaders)
add_library(PandaTest::MeshResourceMock ALIAS panda_test_mesh_resource_mock)

panda_add_mock(panda_test_upload_manager_mock
    VULKAN_INTERNAL
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/core/vulkan/resources"
    REPLACES panda::core::vulkan::upload::UploadManager
    API_LIBRARY Panda::Panda
    SOURCES panda/core/UploadManagerStub.cpp
    HEADERS panda/core/UploadManagerMock.hpp
    LIBRARIES PandaTest::MockHeaders)
add_library(PandaTest::UploadManagerMock ALIAS panda_test_upload_manager_mock)

panda_add_mock(panda_test_upload_command_mock
    VULKAN_INTERNAL
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/core/vulkan/resources"
    REPLACES panda::core::vulkan::upload::UploadCommand
    API_LIBRARY Panda::Panda
    SOURCES panda/core/UploadCommandStub.cpp
    HEADERS panda/core/UploadCommandMock.hpp
    LIBRARIES PandaTest::MockHeaders)
add_library(PandaTest::UploadCommandMock ALIAS panda_test_upload_command_mock)

panda_add_mock(panda_test_swapchain_generation_mock
    VULKAN_INTERNAL
    REPLACES panda::core::vulkan::createSwapchainGeneration
    API_LIBRARY Panda::Panda
    SOURCES panda/core/SwapchainGenerationStub.cpp
    HEADERS panda/core/SwapchainGenerationMock.hpp
    LIBRARIES PandaTest::MockHeaders)
if(MSVC AND PANDA_ENABLE_SANITIZER_ADDRESS AND PANDA_WARNINGS_AS_ERRORS)
    # gMock's unreachable default-action fallback triggers C4702 in this mock
    # under MSVC ASan; keep warnings-as-errors enabled for every other warning.
    target_compile_options(panda_test_swapchain_generation_mock PRIVATE /wd4702)
endif()
add_library(PandaTest::SwapchainGenerationMock ALIAS panda_test_swapchain_generation_mock)

panda_add_mock(panda_test_submission_queue_mock
    VULKAN_INTERNAL
    REPLACES panda::detail::SubmissionQueue
    API_LIBRARY Panda::Panda
    SOURCES panda/core/SubmissionQueueStub.cpp
    HEADERS panda/core/SubmissionQueueMock.hpp
    LIBRARIES PandaTest::MockHeaders)
add_library(PandaTest::SubmissionQueueMock ALIAS panda_test_submission_queue_mock)

panda_add_mock(panda_test_core_version_mock
    REPLACES panda::version
    API_LIBRARY Panda::Panda
    SOURCES panda/core/VersionStub.cpp
    HEADERS panda/core/VersionMock.hpp
    LIBRARIES PandaTest::MockHeaders)
add_library(PandaTest::CoreVersionMock ALIAS panda_test_core_version_mock)

if(PANDA_BUILD_CUDA)
    panda_add_mock(panda_test_cuda_runtime_mock
        REPLACES cudaGetDeviceCount
        API_LIBRARY CUDA::cudart_static
        SOURCES external/cuda/CudaRuntimeStub.cpp
        HEADERS external/cuda/CudaRuntimeMock.hpp
        LIBRARIES PandaTest::MockHeaders)
    add_library(PandaTest::CudaRuntimeMock ALIAS panda_test_cuda_runtime_mock)
endif()
