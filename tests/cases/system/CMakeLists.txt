if(TARGET Panda::Tools)
    panda_add_test(panda_system_application_startup system
        SOURCES ApplicationStartup.cpp
        HEADERS "${PROJECT_SOURCE_DIR}/tests/support/GlfwTestSupport.hpp"
        LIBRARIES Panda::Tools PandaTest::Support glfw)

    panda_add_test(panda_system_window_rendering system
        SOURCES WindowRendering.cpp
        HEADERS "${PROJECT_SOURCE_DIR}/tests/support/EnvironmentLighting.hpp"
                "${PROJECT_SOURCE_DIR}/tests/support/GlfwTestSupport.hpp"
                "${PROJECT_SOURCE_DIR}/tests/support/TestGeometry.hpp"
        LIBRARIES Panda::Tools PandaTest::Support PandaTest::LogCapture glfw)
endif()
if(TARGET Panda::Panda)
    panda_add_test(panda_system_vulkan_engine system
        SOURCES VulkanEngine.cpp
        LIBRARIES Panda::Panda Vulkan::Headers)

    panda_add_test(panda_system_offscreen_rendering system
        VULKAN_INTERNAL
        SOURCES OffscreenRendering.cpp
        HEADERS "${PROJECT_SOURCE_DIR}/tests/support/EnvironmentLighting.hpp"
        LIBRARIES Panda::Panda PandaTest::LogCapture)

    panda_add_test(panda_system_buffer system
        SOURCES Buffer.cpp
        LIBRARIES Panda::Panda PandaTest::Support PandaTest::LogCapture)

    panda_add_test(panda_system_external_buffer system
        SOURCES ExternalBuffer.cpp
        LIBRARIES Panda::Panda)

    panda_add_test(panda_system_texture system
        SOURCES Texture.cpp
        LIBRARIES Panda::Panda PandaTest::Support PandaTest::LogCapture)

    panda_add_test(panda_system_mesh system
        SOURCES Mesh.cpp
        HEADERS "${PROJECT_SOURCE_DIR}/tests/support/TestGeometry.hpp"
        LIBRARIES Panda::Panda PandaTest::Support PandaTest::LogCapture)

    panda_add_test(panda_system_upload system
        VULKAN_INTERNAL
        SOURCES Upload.cpp
        LIBRARIES Panda::Panda PandaTest::Support PandaTest::LogCapture)

    panda_add_test(panda_system_memory_allocation system
        VULKAN_INTERNAL
        INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/core/vulkan/resources"
        SOURCES MemoryAllocation.cpp
        LIBRARIES Panda::Panda Vulkan::Headers)
endif()

if(TARGET Panda::Scene)
    panda_add_test(panda_system_scene system
        SOURCES SceneRendering.cpp
        HEADERS "${PROJECT_SOURCE_DIR}/tests/support/TestGeometry.hpp"
        LIBRARIES Panda::Scene PandaTest::Support)
endif()

if(TARGET Panda::Cuda)
    panda_add_test(panda_system_external_buffer_handoff system
        TIMEOUT 30
        VULKAN_INTERNAL
        SOURCES ExternalBufferHandoff.cpp VulkanBufferReadback.cpp
        HEADERS VulkanBufferReadback.hpp
        LIBRARIES Panda::Cuda PandaTest::LogCapture)

    panda_add_test(panda_system_external_buffer_cuda system
        SOURCES ExternalBufferCuda.cpp
        LIBRARIES Panda::Panda Panda::Cuda
        MOCKS PandaTest::CudaRuntimeMock)
endif()
