panda_add_test(panda_integration_core integration
    SOURCES ImplementationSubstitution.cpp
    LIBRARIES Panda::Panda PandaTest::LogCapture
    MOCKS PandaTest::CoreVersionMock)


panda_add_test(panda_integration_presentation integration
    VULKAN_INTERNAL
    SOURCES PresentationSubmission.cpp
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
    LIBRARIES Panda::Panda PandaTest::Support PandaTest::LogCapture
    MOCKS PandaTest::VulkanMock PandaTest::SwapchainGenerationMock)

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
