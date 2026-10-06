add_library(panda_test_mock_headers INTERFACE)
set_target_properties(panda_test_mock_headers PROPERTIES FOLDER "Panda/Test support")
add_library(PandaTest::MockHeaders ALIAS panda_test_mock_headers)
target_link_libraries(panda_test_mock_headers INTERFACE PandaTest::Support)
target_include_directories(panda_test_mock_headers INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")

panda_add_mock(panda_test_glfw_mock
    REPLACES glfw
    SOURCES external/glfw/GlfwStub.cpp
    LIBRARIES PandaTest::MockHeaders)
add_library(PandaTest::GlfwMock ALIAS panda_test_glfw_mock)

# The unit boundary is Vulkan as a library, not an individual entry point.
# The stub provides the calls controlled by current Core unit scenarios.
panda_add_mock(panda_test_vulkan_mock
    REPLACES Vulkan::Vulkan
    API_LIBRARY Vulkan::Headers
    SOURCES external/vulkan/VulkanStub.cpp
    LIBRARIES PandaTest::MockHeaders)
add_library(PandaTest::VulkanMock ALIAS panda_test_vulkan_mock)

panda_add_mock(panda_test_swapchain_generation_mock
    REPLACES panda::detail::createSwapchainGeneration
    API_LIBRARY Panda::Panda
    SOURCES panda/core/SwapchainGenerationStub.cpp
    LIBRARIES PandaTest::MockHeaders)
target_include_directories(panda_test_swapchain_generation_mock PRIVATE "${PROJECT_SOURCE_DIR}/src/core/vulkan")
target_compile_definitions(panda_test_swapchain_generation_mock PRIVATE
    VULKAN_HPP_NO_CONSTRUCTORS
    VULKAN_HPP_NO_EXCEPTIONS)
if(MSVC AND PANDA_ENABLE_SANITIZER_ADDRESS AND PANDA_WARNINGS_AS_ERRORS)
    # gMock's unreachable default-action fallback triggers C4702 in this mock
    # under MSVC ASan; keep warnings-as-errors enabled for every other warning.
    target_compile_options(panda_test_swapchain_generation_mock PRIVATE /wd4702)
endif()
add_library(PandaTest::SwapchainGenerationMock ALIAS panda_test_swapchain_generation_mock)

panda_add_mock(panda_test_core_version_mock
    REPLACES panda::version
    API_LIBRARY Panda::Panda
    SOURCES panda/core/VersionStub.cpp
    LIBRARIES PandaTest::MockHeaders)
add_library(PandaTest::CoreVersionMock ALIAS panda_test_core_version_mock)

if(PANDA_BUILD_CUDA)
    panda_add_mock(panda_test_cuda_runtime_mock
        REPLACES cudaGetDeviceCount
        API_LIBRARY CUDA::cudart_static
        SOURCES external/cuda/CudaRuntimeStub.cpp
        LIBRARIES PandaTest::MockHeaders)
    add_library(PandaTest::CudaRuntimeMock ALIAS panda_test_cuda_runtime_mock)
endif()
