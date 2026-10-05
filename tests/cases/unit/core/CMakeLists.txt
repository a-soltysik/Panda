panda_add_test(panda_unit_core unit
    SOURCES Version.cpp DeviceSelection.cpp SwapchainGeneration.cpp
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

panda_add_test(panda_unit_presentation unit
    SOURCES Presentation.cpp
    LIBRARIES Panda::Panda PandaTest::Support
    MOCKS PandaTest::VulkanMock PandaTest::SwapchainGenerationMock)
target_include_directories(panda_unit_presentation PRIVATE "${PROJECT_SOURCE_DIR}/src/core/vulkan")
target_compile_definitions(panda_unit_presentation PRIVATE
    VULKAN_HPP_NO_CONSTRUCTORS
    VULKAN_HPP_NO_EXCEPTIONS)
