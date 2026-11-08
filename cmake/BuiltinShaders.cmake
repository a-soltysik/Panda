function(panda_add_builtin_shader_command output_name source_name)
    set(shader_source "${PROJECT_SOURCE_DIR}/shaders/builtin/${source_name}")
    set(shader_spirv "${PANDA_BUILTIN_SHADER_DIRECTORY}/${output_name}.spv")
    set(PANDA_BUILTIN_SHADER_SOURCES ${PANDA_BUILTIN_SHADER_SOURCES} "${shader_source}" PARENT_SCOPE)
    set(PANDA_BUILTIN_SHADER_SPIRV_FILES ${PANDA_BUILTIN_SHADER_SPIRV_FILES} "${shader_spirv}" PARENT_SCOPE)
    set(PANDA_BUILTIN_SHADER_COMMANDS
        ${PANDA_BUILTIN_SHADER_COMMANDS}
        COMMAND "${PANDA_GLSLC_EXECUTABLE}" --target-env=vulkan1.3 -I "${PROJECT_SOURCE_DIR}/shaders/builtin"
                -I "${PROJECT_SOURCE_DIR}/src/core/vulkan/renderer"
                ${ARGN} "${shader_source}" -o "${shader_spirv}"
        COMMAND "${PANDA_SPIRV_VAL_EXECUTABLE}" --target-env vulkan1.3 "${shader_spirv}"
        PARENT_SCOPE)
endfunction()

function(panda_add_builtin_shaders target)
    find_program(PANDA_GLSLC_EXECUTABLE NAMES glslc REQUIRED)
    find_program(PANDA_SPIRV_VAL_EXECUTABLE NAMES spirv-val REQUIRED)

    set(PANDA_BUILTIN_SHADER_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
    set(PANDA_BUILTIN_SHADER_HEADER "${PANDA_BUILTIN_SHADER_DIRECTORY}/BuiltinShaders.hpp")
    set(PANDA_BUILTIN_SHADER_SOURCES)
    set(PANDA_BUILTIN_SHADER_SPIRV_FILES)
    set(PANDA_BUILTIN_SHADER_COMMANDS)

    panda_add_builtin_shader_command(mesh_lit_color.vert mesh_lit_color.vert)
    panda_add_builtin_shader_command(mesh_lit_color_texture.vert mesh_lit_color.vert -DHAS_TEXTURE_COORDINATES=1)
    panda_add_builtin_shader_command(mesh_lit_plain.vert mesh_lit_plain.vert)
    panda_add_builtin_shader_command(mesh_lit_plain_texture.vert mesh_lit_plain.vert -DHAS_TEXTURE_COORDINATES=1)
    panda_add_builtin_shader_command(mesh_unlit_color.vert mesh_unlit_color.vert)
    panda_add_builtin_shader_command(mesh_unlit_color_texture.vert mesh_unlit_color.vert -DHAS_TEXTURE_COORDINATES=1)
    panda_add_builtin_shader_command(mesh_unlit_plain.vert mesh_unlit_plain.vert)
    panda_add_builtin_shader_command(mesh_unlit_plain_texture.vert mesh_unlit_plain.vert -DHAS_TEXTURE_COORDINATES=1)
    # Keep discard compatible with devices that lack shaderDemoteToHelperInvocation.
    panda_add_builtin_shader_command(mesh.frag mesh.frag --target-spv=spv1.3)
    panda_add_builtin_shader_command(shadow.vert shadow.vert)
    panda_add_builtin_shader_command(shadow_texture.vert shadow.vert -DHAS_BASE_COLOR_TEXTURE=1)
    panda_add_builtin_shader_command(shadow_vertex_alpha.vert shadow_vertex_alpha.vert)
    panda_add_builtin_shader_command(shadow_vertex_alpha_texture.vert shadow_vertex_alpha.vert -DHAS_BASE_COLOR_TEXTURE=1)
    panda_add_builtin_shader_command(shadow.frag shadow.frag --target-spv=spv1.3)
    panda_add_builtin_shader_command(shadow_texture.frag shadow.frag -DHAS_BASE_COLOR_TEXTURE=1 --target-spv=spv1.3)
    list(REMOVE_DUPLICATES PANDA_BUILTIN_SHADER_SOURCES)
    list(APPEND PANDA_BUILTIN_SHADER_SOURCES "${PROJECT_SOURCE_DIR}/shaders/builtin/mesh_vertex_common.glsl")
    list(APPEND PANDA_BUILTIN_SHADER_SOURCES "${PROJECT_SOURCE_DIR}/shaders/builtin/shadow_vertex_common.glsl")
    list(APPEND PANDA_BUILTIN_SHADER_SOURCES "${PROJECT_SOURCE_DIR}/src/core/vulkan/renderer/ShaderLocations.inc")
    add_custom_command(
        OUTPUT "${PANDA_BUILTIN_SHADER_HEADER}" ${PANDA_BUILTIN_SHADER_SPIRV_FILES}
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${PANDA_BUILTIN_SHADER_DIRECTORY}"
        ${PANDA_BUILTIN_SHADER_COMMANDS}
        COMMAND "${CMAKE_COMMAND}"
                "-DOUTPUT_FILE=${PANDA_BUILTIN_SHADER_HEADER}"
                "-DSHADER_SPIRV_FILES=${PANDA_BUILTIN_SHADER_SPIRV_FILES}"
                -P "${PROJECT_SOURCE_DIR}/cmake/EmbedSpirv.cmake"
        DEPENDS
            ${PANDA_BUILTIN_SHADER_SOURCES}
            "${PROJECT_SOURCE_DIR}/cmake/EmbedSpirv.cmake"
        VERBATIM)
    add_custom_target(panda_builtin_shaders DEPENDS "${PANDA_BUILTIN_SHADER_HEADER}")
    add_dependencies(${target} panda_builtin_shaders)
    target_include_directories(${target} PRIVATE "${PANDA_BUILTIN_SHADER_DIRECTORY}")

    if(TARGET panda_static_analysis)
        string(SHA256 mesh_renderer_analysis_id "${CMAKE_CURRENT_SOURCE_DIR}/vulkan/renderer/MeshRenderer.cpp")
        add_dependencies(panda_analysis_${mesh_renderer_analysis_id} panda_builtin_shaders)
    endif()
endfunction()
