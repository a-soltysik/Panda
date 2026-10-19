# Only core embeds these objects; add-ons use core.
panda_add_component(panda_common
    TYPE OBJECT
    INCLUDE_DIRECTORY "${PROJECT_SOURCE_DIR}/include/common"
    SOURCES Logger.cpp Assert.cpp FunctionName.cpp SourcePath.cpp Stacktrace.cpp HostInfo.cpp
    PUBLIC_HEADERS panda/Assert.hpp panda/Logger.hpp panda/HostInfo.hpp
    PRIVATE_HEADERS FunctionName.hpp SourcePath.hpp Stacktrace.hpp)
target_compile_definitions(panda_common PRIVATE
    PANDA_SOURCE_DIRECTORY="${PROJECT_SOURCE_DIR}/")
if(WIN32)
    target_link_libraries(panda_common PUBLIC Advapi32)
endif()

if(PANDA_ENABLE_STACKTRACE)
    target_compile_definitions(panda_common PRIVATE PANDA_ENABLE_STACKTRACE)
    if(NOT PANDA_STACKTRACE_DIRECT)
        target_link_libraries(panda_common PUBLIC stdc++exp)
    endif()
endif()
