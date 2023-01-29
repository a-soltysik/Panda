set(TARGET_NAME ${PROJECT_NAME}.Engine)

file(GLOB_RECURSE ${TARGET_NAME}_SRC_LIST
    ${SRC_DIR}/*.h
    ${SRC_DIR}/*.cpp
    )

add_executable(${TARGET_NAME} ${${TARGET_NAME}_SRC_LIST})

target_include_directories(${TARGET_NAME} PRIVATE ${SRC_DIR})

find_package(Vulkan REQUIRED)
target_link_libraries(${TARGET_NAME} PRIVATE Vulkan::Vulkan glfw glm::glm)

target_compile_definitions(${TARGET_NAME} PRIVATE PROJECT_NAME=\"$<TARGET_PROPERTY:NAME>\")
target_compile_definitions(${TARGET_NAME} PRIVATE GLFW_INCLUDE_VULKAN)

target_precompile_headers(${TARGET_NAME}
    PRIVATE
    <GLFW/glfw3.h>
    <vector>
    )
