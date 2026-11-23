function(panda_add_shaders target)
    cmake_parse_arguments(SHADERS "" "NAMESPACE" "SOURCES;INCLUDE_DIRECTORIES" ${ARGN})
    if(SHADERS_UNPARSED_ARGUMENTS OR SHADERS_KEYWORDS_MISSING_VALUES OR NOT SHADERS_NAMESPACE OR NOT SHADERS_SOURCES)
        message(FATAL_ERROR "Invalid shader resources for ${target}")
    endif()
    if(NOT TARGET "${target}")
        message(FATAL_ERROR "Shader resources require an existing target: ${target}")
    endif()
    if(NOT SHADERS_NAMESPACE MATCHES "^[A-Za-z_][A-Za-z0-9_]*(::[A-Za-z_][A-Za-z0-9_]*)*$")
        message(FATAL_ERROR "Invalid C++ shader namespace: ${SHADERS_NAMESPACE}")
    endif()

    get_property(shader_namespaces TARGET "${target}" PROPERTY PANDA_SHADER_NAMESPACES)
    if(SHADERS_NAMESPACE IN_LIST shader_namespaces)
        message(FATAL_ERROR "Duplicate shader namespace ${SHADERS_NAMESPACE} in target ${target}")
    endif()
    set_property(TARGET "${target}" APPEND PROPERTY PANDA_SHADER_NAMESPACES "${SHADERS_NAMESPACE}")

    find_program(PANDA_GLSLC_EXECUTABLE NAMES glslc REQUIRED)
    find_program(PANDA_SPIRV_VAL_EXECUTABLE NAMES spirv-val REQUIRED)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    string(REPLACE "::" "/" namespace_path "${SHADERS_NAMESPACE}")
    set(shader_root "${CMAKE_CURRENT_BINARY_DIR}/generated/${target}")
    set(shader_directory "${shader_root}/${namespace_path}")
    set(shader_header "${shader_directory}/ShaderResources.hpp")
    set(shader_generator "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/EmbedShaderResources.py")
    set(shader_template "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ShaderResources.hpp.in")
    set(shader_keys)
    set(shader_spirv_files)
    set(shader_arguments)

    set(shader_include_arguments)
    foreach(include_directory IN LISTS SHADERS_INCLUDE_DIRECTORIES)
        if(IS_ABSOLUTE "${include_directory}")
            set(absolute_include_directory "${include_directory}")
        else()
            get_filename_component(absolute_include_directory "${include_directory}" ABSOLUTE
                                   BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        endif()
        if(NOT IS_DIRECTORY "${absolute_include_directory}")
            message(FATAL_ERROR "Shader include directory does not exist: ${absolute_include_directory}")
        endif()
        list(APPEND shader_include_arguments -I "${absolute_include_directory}")
    endforeach()

    foreach(shader IN LISTS SHADERS_SOURCES)
        if(IS_ABSOLUTE "${shader}")
            set(absolute_source "${shader}")
        else()
            get_filename_component(absolute_source "${shader}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        endif()
        if(NOT EXISTS "${absolute_source}")
            message(FATAL_ERROR "Shader source does not exist: ${absolute_source}")
        endif()
        cmake_path(NORMAL_PATH absolute_source OUTPUT_VARIABLE normalized_source)
        cmake_path(RELATIVE_PATH normalized_source BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                   OUTPUT_VARIABLE shader_key)
        if(shader_key MATCHES "^\\.\\.(/|$)" OR IS_ABSOLUTE "${shader_key}")
            message(FATAL_ERROR "Shader source must be inside ${CMAKE_CURRENT_SOURCE_DIR}: ${absolute_source}")
        endif()
        if(shader_key MATCHES "[;\r\n]" OR NOT shader_key MATCHES "\\.(vert|frag)$")
            message(FATAL_ERROR "Shader key must be a relative .vert or .frag path: ${shader_key}")
        endif()
        if(shader_key IN_LIST shader_keys)
            message(FATAL_ERROR "Duplicate shader key ${shader_key} in namespace ${SHADERS_NAMESPACE}")
        endif()
        list(APPEND shader_keys "${shader_key}")

        set(shader_spirv "${shader_directory}/${shader_key}.spv")
        set(shader_depfile "${shader_spirv}.d")
        get_filename_component(shader_output_directory "${shader_spirv}" DIRECTORY)
        add_custom_command(
            OUTPUT "${shader_spirv}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${shader_output_directory}"
            COMMAND "${PANDA_GLSLC_EXECUTABLE}" --target-env=vulkan1.3 ${shader_include_arguments}
                    -MD -MF "${shader_depfile}" -MT "${shader_spirv}" "${normalized_source}" -o "${shader_spirv}"
            COMMAND "${PANDA_SPIRV_VAL_EXECUTABLE}" --target-env vulkan1.3 "${shader_spirv}"
            DEPENDS "${normalized_source}"
            DEPFILE "${shader_depfile}"
            VERBATIM)
        list(APPEND shader_spirv_files "${shader_spirv}")
        list(APPEND shader_arguments --shader "${shader_key}" "${shader_spirv}")
    endforeach()

    add_custom_command(
        OUTPUT "${shader_header}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${shader_directory}"
        COMMAND "${Python3_EXECUTABLE}" "${shader_generator}"
                --template "${shader_template}"
                --output "${shader_header}"
                --namespace "${SHADERS_NAMESPACE}"
                ${shader_arguments}
        DEPENDS ${shader_spirv_files} "${shader_generator}" "${shader_template}"
        VERBATIM)
    add_custom_target("${target}_shader_resources" DEPENDS "${shader_header}")
    panda_add_static_analysis_dependency("${target}" "${target}_shader_resources")
    target_sources("${target}" PRIVATE "${shader_header}")
    target_include_directories("${target}" PRIVATE "${shader_root}")
endfunction()
