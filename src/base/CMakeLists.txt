panda_add_component(panda_base
    INCLUDE_DIRECTORY "${PROJECT_SOURCE_DIR}/include/base"
    SOURCES Assert.cpp Error.cpp FunctionName.cpp HostInfo.cpp Logger.cpp SourcePath.cpp Stacktrace.cpp
    PUBLIC_HEADERS
        panda/base/Error.hpp
        panda/base/StrongAlias.hpp
        panda/diagnostics/Assert.hpp
        panda/diagnostics/HostInfo.hpp
        panda/diagnostics/Logger.hpp
    PRIVATE_HEADERS FunctionName.hpp Hash.hpp Overloaded.hpp SourcePath.hpp Stacktrace.hpp)
add_library(panda_base_internal INTERFACE)
set_target_properties(panda_base_internal PROPERTIES FOLDER "Panda/Internal")
target_include_directories(panda_base_internal INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(panda_base PRIVATE PANDA_SOURCE_DIRECTORY="${PROJECT_SOURCE_DIR}/")
if(WIN32)
    target_link_libraries(panda_base PRIVATE Advapi32)
endif()

if(PANDA_ENABLE_STACKTRACE)
    target_compile_definitions(panda_base PRIVATE PANDA_ENABLE_STACKTRACE)
    if(NOT PANDA_STACKTRACE_DIRECT)
        target_link_libraries(panda_base PRIVATE stdc++exp)
    endif()
endif()
