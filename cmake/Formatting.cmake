function(panda_enable_format_check target)
    if(PANDA_ENABLE_CLANG_FORMAT)
        find_program(PANDA_CLANG_FORMAT_EXECUTABLE NAMES clang-format REQUIRED)
        get_target_property(panda_format_sources ${target} SOURCES)
        list(APPEND panda_format_sources ${ARGN})
        get_target_property(panda_target_type ${target} TYPE)
        if(panda_target_type STREQUAL "OBJECT_LIBRARY")
            add_custom_target(${target}_format_check
                COMMAND "${PANDA_CLANG_FORMAT_EXECUTABLE}" --dry-run --Werror
                    ${panda_format_sources}
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                VERBATIM
            )
            set_target_properties(${target}_format_check PROPERTIES FOLDER "Panda/Checks")
            add_dependencies(${target} ${target}_format_check)
        else()
            add_custom_command(TARGET ${target} PRE_LINK
                COMMAND "${PANDA_CLANG_FORMAT_EXECUTABLE}" --dry-run --Werror
                    ${panda_format_sources}
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                VERBATIM
            )
        endif()
        if(PANDA_BUILD_TESTS)
            add_test(NAME ${target}_format
                COMMAND "${PANDA_CLANG_FORMAT_EXECUTABLE}" --dry-run --Werror
                    ${panda_format_sources})
            set_tests_properties(${target}_format PROPERTIES
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        endif()
    endif()
endfunction()
