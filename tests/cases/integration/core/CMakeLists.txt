panda_add_test(panda_integration_core integration
    SOURCES ImplementationSubstitution.cpp
    LIBRARIES Panda::Panda PandaTest::LogCapture
    MOCKS PandaTest::CoreVersionMock)


panda_add_test(panda_integration_presentation integration
    SOURCES PresentationSubmission.cpp
    LIBRARIES Panda::Panda PandaTest::Support
    MOCKS PandaTest::VulkanMock PandaTest::SwapchainGenerationMock)
target_include_directories(panda_integration_presentation PRIVATE "${PROJECT_SOURCE_DIR}/src/core/vulkan")
target_compile_definitions(panda_integration_presentation PRIVATE VULKAN_HPP_NO_CONSTRUCTORS VULKAN_HPP_NO_EXCEPTIONS)

panda_add_test(panda_integration_upload integration
    SOURCES BufferUpload.cpp
    LIBRARIES Panda::Panda PandaTest::Support
    MOCKS PandaTest::VulkanMock)
target_include_directories(panda_integration_upload PRIVATE "${PROJECT_SOURCE_DIR}/src/core/vulkan")
target_compile_definitions(panda_integration_upload PRIVATE VULKAN_HPP_NO_CONSTRUCTORS VULKAN_HPP_NO_EXCEPTIONS)


panda_add_test(panda_integration_context_resources integration
    SOURCES ContextResources.cpp
    LIBRARIES Panda::Panda PandaTest::Support PandaTest::LogCapture
    MOCKS PandaTest::VulkanMock PandaTest::SwapchainGenerationMock)
target_include_directories(panda_integration_context_resources PRIVATE "${PROJECT_SOURCE_DIR}/src/core/vulkan")
target_compile_definitions(panda_integration_context_resources PRIVATE VULKAN_HPP_NO_CONSTRUCTORS VULKAN_HPP_NO_EXCEPTIONS)
