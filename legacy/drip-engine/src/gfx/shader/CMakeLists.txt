set(SHADER_SRC_DIR ${CMAKE_CURRENT_LIST_DIR})

set(SHADER_SRC_LIST
        ${SHADER_SRC_DIR}/vs/Mesh.vert
        ${SHADER_SRC_DIR}/vs/Particles.vert
        ${SHADER_SRC_DIR}/fs/Mesh.frag
        ${SHADER_SRC_DIR}/fs/Particles.frag)


set(SHADERS_INCLUDE_DIR "${CMAKE_BINARY_DIR}/configured_files/include/drip/gfx/shaders")

foreach (GLSL_FILE ${SHADER_SRC_LIST})
    get_filename_component(FILE_NAME ${GLSL_FILE} NAME)
    set(SPIRV "${CMAKE_BINARY_DIR}/shader/${FILE_NAME}.spv")

    add_custom_command(
            OUTPUT ${SPIRV}
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/shader"
            COMMAND ${Vulkan_GLSLC_EXECUTABLE} ${GLSL_FILE} -o ${SPIRV}
            DEPENDS ${GLSL_FILE})

    set(HEADER "${SHADERS_INCLUDE_DIR}/${FILE_NAME}.spv.hpp")
    add_custom_command(
            OUTPUT ${HEADER}
            COMMAND ${CMAKE_COMMAND}
            -DSPIRV_FILE=${SPIRV}
            -DCONFIG_FILE=${CMAKE_CURRENT_LIST_DIR}/config/Shader.hpp.in
            -DHEADER=${HEADER}
            -P ${CMAKE_SOURCE_DIR}/cmake/GenerateShaderHeader.cmake
            DEPENDS ${SPIRV}
    )

    list(APPEND SHADER_HEADERS ${HEADER})

endforeach ()

set_source_files_properties(${SHADER_HEADERS} PROPERTIES GENERATED TRUE)
add_custom_target(drip_shader DEPENDS ${SHADER_HEADERS})
set_target_properties(drip_shader PROPERTIES FOLDER "Shaders")