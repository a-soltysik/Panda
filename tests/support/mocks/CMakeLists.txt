add_library(panda_test_mock_headers INTERFACE)
add_library(PandaTest::MockHeaders ALIAS panda_test_mock_headers)
target_link_libraries(panda_test_mock_headers INTERFACE PandaTest::Support)
target_include_directories(panda_test_mock_headers INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")

panda_add_mock(panda_test_glfw_mock
    REPLACES glfw
    SOURCES external/glfw/GlfwStub.cpp
    LIBRARIES PandaTest::MockHeaders)
add_library(PandaTest::GlfwMock ALIAS panda_test_glfw_mock)

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
