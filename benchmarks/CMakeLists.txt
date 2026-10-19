add_executable(panda-bench
    src/Arguments.cpp
    src/Report.cpp
    src/Statistics.cpp
    src/main.cpp
    src/Benchmark.cpp
    src/Benchmark.hpp
    src/SmokeRun.cpp
    src/SmokeRun.hpp
    src/SmokeRunBackend.hpp
    src/VulkanSmokeRunBackend.cpp
    src/VulkanSmokeRunBackend.hpp
    src/panda_bench/Arguments.hpp
    src/panda_bench/Report.hpp
    src/panda_bench/Statistics.hpp
)
set_target_properties(panda-bench PROPERTIES
    UNITY_BUILD "${PANDA_ENABLE_UNITY}"
    FOLDER "Panda/Benchmarks"
)
target_include_directories(panda-bench PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(panda-bench PRIVATE Panda::Panda Panda::Tools argparse::argparse nlohmann_json::nlohmann_json)

execute_process(
    COMMAND git rev-parse --short=12 HEAD
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    OUTPUT_VARIABLE PANDA_BENCH_REVISION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT PANDA_BENCH_REVISION)
    set(PANDA_BENCH_REVISION "unknown")
endif()
target_compile_definitions(panda-bench PRIVATE
    "PANDA_BENCH_REVISION=\"${PANDA_BENCH_REVISION}\""
    "PANDA_BENCH_COMPILER=\"${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}\""
    "PANDA_BENCH_OS=\"${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_VERSION}\""
    "PANDA_BENCH_BUILD_MODE=\"$<CONFIG>\""
)
panda_configure_target(panda-bench
    "${CMAKE_CURRENT_SOURCE_DIR}/src/panda_bench/Arguments.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/panda_bench/Report.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/panda_bench/Statistics.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Benchmark.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/SmokeRun.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/SmokeRunBackend.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/VulkanSmokeRunBackend.hpp"
)
