function(panda_configure_target target)
    set_target_properties(${target} PROPERTIES CXX_SCAN_FOR_MODULES OFF)
    panda_apply_warnings(${target})
    panda_enable_coverage(${target})
    panda_enable_sanitizers(${target})
    panda_enable_static_analysis(${target})
    panda_enable_format_check(${target} ${ARGN})
endfunction()

function(panda_add_component target)
    cmake_parse_arguments(COMPONENT "" "TYPE;ALIAS;INCLUDE_DIRECTORY"
        "SOURCES;LIBRARIES" ${ARGN})
    if(COMPONENT_UNPARSED_ARGUMENTS OR COMPONENT_KEYWORDS_MISSING_VALUES OR NOT COMPONENT_INCLUDE_DIRECTORY)
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
    file(GLOB_RECURSE public_headers CONFIGURE_DEPENDS "${include_directory}/*.hpp")
    target_sources(${target} PUBLIC FILE_SET HEADERS
        BASE_DIRS "${include_directory}"
        FILES ${public_headers})
    if(PANDA_VERIFY_PUBLIC_HEADERS)
        set_target_properties(${target} PROPERTIES VERIFY_INTERFACE_HEADER_SETS ON)
    endif()
    if(COMPONENT_LIBRARIES)
        target_link_libraries(${target} ${COMPONENT_LIBRARIES})
    endif()
    file(GLOB_RECURSE private_headers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp")
    panda_configure_target(${target} ${public_headers} ${private_headers})
endfunction()
