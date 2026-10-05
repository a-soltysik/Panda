panda_add_component(panda_core
    ALIAS Panda::Panda
    INCLUDE_DIRECTORY "${PROJECT_SOURCE_DIR}/include/core"
    SOURCES
        Panda.cpp
        Error.cpp
        vulkan/Context.cpp
        vulkan/DeviceSelection.cpp
        vulkan/Presentation.cpp
        vulkan/SwapchainGeneration.cpp
        vulkan/VulkanError.cpp
    LIBRARIES PUBLIC panda_common glm::glm Vulkan::Headers PRIVATE Vulkan::Vulkan)

set_target_properties(panda_core PROPERTIES OUTPUT_NAME panda)
target_compile_definitions(panda_core PRIVATE PANDA_VERSION_TEXT="${PROJECT_VERSION}"
    VULKAN_HPP_NO_EXCEPTIONS VULKAN_HPP_NO_CONSTRUCTORS)
