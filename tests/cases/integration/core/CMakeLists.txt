panda_add_test(panda_integration_core integration
    SOURCES ImplementationSubstitution.cpp
    LIBRARIES Panda::Panda PandaTest::LogCapture
    MOCKS PandaTest::CoreVersionMock)


panda_add_test(panda_integration_presentation integration
    VULKAN_INTERNAL
    SOURCES PresentationSubmission.cpp
    HEADERS "${PROJECT_SOURCE_DIR}/tests/support/PresentationTestSupport.hpp"
            "${PROJECT_SOURCE_DIR}/tests/support/VulkanTestSupport.hpp"
            "${PROJECT_SOURCE_DIR}/tests/support/mocks/panda/core/WindowSurfaceMock.hpp"
    LIBRARIES Panda::Panda PandaTest::Support
    MOCKS PandaTest::VulkanMock PandaTest::SwapchainGenerationMock)

panda_add_test(panda_integration_upload integration
    VULKAN_INTERNAL
    SOURCES BufferUpload.cpp
    LIBRARIES Panda::Panda PandaTest::Support
    MOCKS PandaTest::VulkanMock)


panda_add_test(panda_integration_context_resources integration
    VULKAN_INTERNAL
    SOURCES ContextResources.cpp
    HEADERS "${PROJECT_SOURCE_DIR}/tests/support/mocks/panda/core/WindowSurfaceMock.hpp"
    LIBRARIES Panda::Panda PandaTest::Support PandaTest::LogCapture
    MOCKS PandaTest::VulkanMock PandaTest::SwapchainGenerationMock)
panda_add_shaders(panda_integration_context_resources
    NAMESPACE test::program
    SOURCES fixtures/shaders/program_camera.vert fixtures/shaders/program_camera.frag
            fixtures/shaders/readonly_storage.vert fixtures/shaders/program_wrong_shadow.vert
            fixtures/shaders/program_shadow_empty.vert)

panda_add_test(panda_integration_memory_block integration
    VULKAN_INTERNAL
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/core/vulkan/resources"
    SOURCES MemoryBlock.cpp
    LIBRARIES Panda::Panda PandaTest::Support
    MOCKS PandaTest::VulkanMock)

panda_add_test(panda_integration_memory_pool integration
    VULKAN_INTERNAL
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/core/vulkan/resources"
    SOURCES MemoryPool.cpp
    LIBRARIES Panda::Panda PandaTest::Support
    MOCKS PandaTest::VulkanMock)

panda_add_test(panda_integration_texture_resource integration
    VULKAN_INTERNAL
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/core/vulkan/resources"
    SOURCES TextureResource.cpp
    LIBRARIES Panda::Panda PandaTest::Support
    MOCKS PandaTest::VulkanMock)

panda_add_test(panda_integration_shader_resources integration
    VULKAN_INTERNAL
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/core" "${PROJECT_SOURCE_DIR}/src/core/vulkan/resources"
                         "${PROJECT_BINARY_DIR}/src/core/generated"
    SOURCES ShaderResources.cpp ShaderReflection.cpp FrameBindings.cpp MeshPipelineCache.cpp
    LIBRARIES Panda::Panda PandaTest::Support
    MOCKS PandaTest::VulkanMock PandaTest::BufferResourceMock PandaTest::TextureResourceMock
          PandaTest::SubmissionQueueMock)
panda_add_static_analysis_dependency(panda_integration_shader_resources panda_builtin_shaders)
add_dependencies(panda_integration_shader_resources panda_builtin_shaders)
panda_add_shaders(panda_integration_shader_resources
    NAMESPACE test::shader
    SOURCES fixtures/shaders/basic.vert fixtures/shaders/basic.frag
            fixtures/shaders/descriptor_array.frag fixtures/shaders/component.frag
            fixtures/shaders/readonly_storage.vert fixtures/shaders/mixed_storage.frag
            fixtures/shaders/int64.vert
            fixtures/shaders/sampled_image.vert fixtures/shaders/sampled_cube.frag
            fixtures/shaders/sampled_integer.frag
            fixtures/shaders/left/marker.vert fixtures/shaders/right/marker.vert
    INCLUDE_DIRECTORIES fixtures/shaders)
