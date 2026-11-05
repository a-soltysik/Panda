if(NOT DEFINED PANDA_SOURCE_DIR OR NOT DEFINED PANDA_OUTPUT_FILE)
    message(FATAL_ERROR "PANDA_SOURCE_DIR and PANDA_OUTPUT_FILE are required")
endif()

execute_process(
    COMMAND git rev-parse --short=12 HEAD
    WORKING_DIRECTORY "${PANDA_SOURCE_DIR}"
    OUTPUT_VARIABLE revision
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE result
)
if(NOT "${result}" STREQUAL "0" OR NOT revision)
    set(revision "unknown")
endif()

get_filename_component(output_directory "${PANDA_OUTPUT_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${output_directory}")
set(content "#pragma once\n#define PANDA_BENCH_REVISION \"${revision}\"\n")
file(CONFIGURE OUTPUT "${PANDA_OUTPUT_FILE}" CONTENT "${content}")
