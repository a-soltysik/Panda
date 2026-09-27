cmake_minimum_required(VERSION 3.28)

include(cmake/MsvcEnvironment.cmake)
project(Panda VERSION 0.1.0 LANGUAGES CXX)

include(cmake/Options.cmake)
if(PANDA_BUILD_EXAMPLES AND NOT PANDA_BUILD_TOOLS)
    message(FATAL_ERROR "PANDA_BUILD_EXAMPLES requires PANDA_BUILD_TOOLS=ON for simple_scene")
endif()
include(cmake/CompilerWarnings.cmake)
include(cmake/Sanitizers.cmake)
include(cmake/Stacktrace.cmake)
include(cmake/Dependencies.cmake)
include(cmake/StaticAnalysis.cmake)
include(cmake/Formatting.cmake)
include(cmake/Targets.cmake)

if(PANDA_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(src)

if(PANDA_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(PANDA_BUILD_TESTS)
    add_subdirectory(tests)
endif()
