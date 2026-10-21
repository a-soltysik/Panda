function(panda_configure_target target)
    set_target_properties(${target} PROPERTIES CXX_SCAN_FOR_MODULES OFF)
    if(PANDA_EXPERIMENT_MSVC_SYNTAX_ONLY)
        set(syntax_only_launcher ${PANDA_MSVC_LAUNCHER}
            "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/cmake/MsvcSyntaxOnlyRun.cmake" --)
        set_property(TARGET ${target} PROPERTY CXX_COMPILER_LAUNCHER "${syntax_only_launcher}")
    endif()
    # Template-heavy Vulkan/gMock code can exceed the ordinary COFF section limit.
    target_compile_options(${target} PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/bigobj>")
    panda_apply_warnings(${target})
    panda_enable_coverage(${target})
    panda_enable_sanitizers(${target})
    panda_enable_static_analysis(${target})
    panda_enable_format_check(${target} ${ARGN})
endfunction()

function(panda_configure_vulkan_internal target)
    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/src/core/vulkan")
    target_compile_definitions(${target} PRIVATE
        VULKAN_HPP_NO_CONSTRUCTORS
        VULKAN_HPP_NO_EXCEPTIONS)
endfunction()

function(panda_resolve_files output_variable base_directory)
    set(resolved_files "")
    foreach(file IN LISTS ARGN)
        if(IS_ABSOLUTE "${file}")
            set(absolute_file "${file}")
        else()
            get_filename_component(absolute_file "${file}" ABSOLUTE BASE_DIR "${base_directory}")
        endif()
        if(NOT EXISTS "${absolute_file}")
            message(FATAL_ERROR "Declared file does not exist: ${absolute_file}")
        endif()
        list(APPEND resolved_files "${absolute_file}")
    endforeach()
    set(${output_variable} "${resolved_files}" PARENT_SCOPE)
endfunction()

function(panda_add_component target)
    cmake_parse_arguments(COMPONENT "" "TYPE;ALIAS;INCLUDE_DIRECTORY"
        "SOURCES;LIBRARIES;PUBLIC_HEADERS;PRIVATE_HEADERS" ${ARGN})
    if(COMPONENT_UNPARSED_ARGUMENTS OR COMPONENT_KEYWORDS_MISSING_VALUES OR
       NOT COMPONENT_INCLUDE_DIRECTORY OR NOT COMPONENT_PUBLIC_HEADERS)
        message(FATAL_ERROR "Invalid arguments for component ${target}")
    endif()
    if(NOT COMPONENT_TYPE)
        set(COMPONENT_TYPE STATIC)
    endif()
    if(NOT COMPONENT_TYPE MATCHES "^(STATIC|OBJECT)$")
        message(FATAL_ERROR "Component ${target}: unsupported type ${COMPONENT_TYPE}")
    endif()
    add_library(${target} ${COMPONENT_TYPE} ${COMPONENT_SOURCES})
    set_target_properties(${target} PROPERTIES
        UNITY_BUILD "${PANDA_ENABLE_UNITY}"
        FOLDER "Panda/Libraries")
    # Mock objects replace archive members, so test builds need separate translation units.
    set_source_files_properties(${COMPONENT_SOURCES} PROPERTIES
        SKIP_UNITY_BUILD_INCLUSION "${PANDA_BUILD_TESTS}")
    target_compile_features(${target} PUBLIC cxx_std_23)
    if(COMPONENT_ALIAS)
        add_library(${COMPONENT_ALIAS} ALIAS ${target})
    endif()
    get_filename_component(include_directory "${COMPONENT_INCLUDE_DIRECTORY}"
        ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    target_include_directories(${target} PUBLIC "$<BUILD_INTERFACE:${include_directory}>")
    panda_resolve_files(public_headers "${include_directory}" ${COMPONENT_PUBLIC_HEADERS})
    panda_resolve_files(private_headers "${CMAKE_CURRENT_SOURCE_DIR}" ${COMPONENT_PRIVATE_HEADERS})
    target_sources(${target} PUBLIC FILE_SET HEADERS
        BASE_DIRS "${include_directory}"
        FILES ${public_headers})
    if(private_headers)
        target_sources(${target} PRIVATE ${private_headers})
    endif()
    if(PANDA_VERIFY_PUBLIC_HEADERS)
        set_target_properties(${target} PROPERTIES VERIFY_INTERFACE_HEADER_SETS ON)
    endif()
    if(COMPONENT_LIBRARIES)
        target_link_libraries(${target} ${COMPONENT_LIBRARIES})
    endif()
    panda_configure_target(${target} ${public_headers} ${private_headers})
endfunction()
