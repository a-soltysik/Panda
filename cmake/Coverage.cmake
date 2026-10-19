if(PANDA_ENABLE_COVERAGE)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        message(FATAL_ERROR "PANDA_ENABLE_COVERAGE requires Linux GCC")
    endif()
    if(NOT PANDA_BUILD_TESTS)
        message(FATAL_ERROR "PANDA_ENABLE_COVERAGE requires PANDA_BUILD_TESTS=ON")
    endif()
    if(PANDA_ENABLE_SANITIZER_ADDRESS OR PANDA_ENABLE_SANITIZER_UNDEFINED_BEHAVIOR
            OR PANDA_ENABLE_SANITIZER_THREAD OR PANDA_ENABLE_SANITIZER_MEMORY
            OR PANDA_ENABLE_SANITIZER_LEAK)
        message(FATAL_ERROR "PANDA_ENABLE_COVERAGE uses a separate build from sanitizers")
    endif()

    find_program(PANDA_GCOVR_EXECUTABLE NAMES gcovr REQUIRED)
    string(REGEX MATCH "^[0-9]+" PANDA_GCC_VERSION_MAJOR "${CMAKE_CXX_COMPILER_VERSION}")
    find_program(PANDA_GCOV_EXECUTABLE NAMES "gcov-${PANDA_GCC_VERSION_MAJOR}" gcov REQUIRED)

    set(panda_coverage_directory "${PROJECT_BINARY_DIR}/coverage")
    add_custom_target(panda_coverage_report
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${panda_coverage_directory}"
        COMMAND "${PANDA_GCOVR_EXECUTABLE}"
            --root "${PROJECT_SOURCE_DIR}"
            --gcov-executable "${PANDA_GCOV_EXECUTABLE}"
            --filter "src/.*"
            --filter "benchmarks/src/.*"
            --exclude-throw-branches
            --html-details "${panda_coverage_directory}/index.html"
            --cobertura "${panda_coverage_directory}/coverage.xml"
            --print-summary
            "${PROJECT_BINARY_DIR}"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        COMMENT "Generating Panda coverage reports"
        VERBATIM
    )
    set_target_properties(panda_coverage_report PROPERTIES FOLDER "Panda/Checks")
endif()

function(panda_enable_coverage target)
    if(NOT PANDA_ENABLE_COVERAGE)
        return()
    endif()

    target_compile_options(${target} PRIVATE --coverage -O0)
    get_target_property(target_type ${target} TYPE)
    if(target_type STREQUAL "STATIC_LIBRARY" OR target_type STREQUAL "OBJECT_LIBRARY")
        target_link_options(${target} INTERFACE --coverage)
    else()
        target_link_options(${target} PRIVATE --coverage)
    endif()
endfunction()
