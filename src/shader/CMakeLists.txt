set(SHADER_SRC_DIR ${SRC_DIR}/shader)

set(SHADER_TARGET_NAME ${ENGINE_TARGET_NAME}.Shader)

if (${CMAKE_HOST_SYSTEM_PROCESSOR} STREQUAL "AMD64")
    set(GLSL_COMPILER "$ENV{VULKAN_SDK}/Bin/glslc.exe")
else ()
    set(GLSL_COMPILER "$ENV{VULKAN_SDK}/Bin32/glslc.exe")
endif ()

file(GLOB_RECURSE ${SHADER_TARGET_NAME}_SRC_LIST
    ${SHADER_SRC_DIR}/*.vert
    ${SHADER_SRC_DIR}/*.tesc
    ${SHADER_SRC_DIR}/*.tese
    ${SHADER_SRC_DIR}/*.geom
    ${SHADER_SRC_DIR}/*.frag
    ${SHADER_SRC_DIR}/*.comp
    )

foreach (GLSL_FILE ${${SHADER_TARGET_NAME}_SRC_LIST})
    get_filename_component(FILE_NAME ${GLSL_FILE} NAME)
    set(OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR})
    set(SPIRV "${OUTPUT_DIR}/${FILE_NAME}.spv")
    add_custom_command(
        OUTPUT ${SPIRV}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${OUTPUT_DIR}"
        COMMAND ${GLSL_COMPILER} ${GLSL_FILE} -o ${SPIRV}
        DEPENDS ${GLSL_FILE}
    )
    list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach (GLSL_FILE)

add_custom_target(
    ${SHADER_TARGET_NAME}
    DEPENDS ${SPIRV_BINARY_FILES}
)

add_dependencies(${ENGINE_TARGET_NAME} ${SHADER_TARGET_NAME})