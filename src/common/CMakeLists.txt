# Only core embeds these objects; add-ons use core.
panda_add_component(panda_common
    TYPE OBJECT
    INCLUDE_DIRECTORY "${PROJECT_SOURCE_DIR}/include/common"
    SOURCES Logger.cpp Assert.cpp FunctionName.cpp SourcePath.cpp Stacktrace.cpp)
target_compile_definitions(panda_common PRIVATE
    PANDA_SOURCE_DIRECTORY="${PROJECT_SOURCE_DIR}/")

if(PANDA_ENABLE_STACKTRACE)
    target_compile_definitions(panda_common PRIVATE PANDA_ENABLE_STACKTRACE)
    if(NOT PANDA_STACKTRACE_DIRECT)
        target_link_libraries(panda_common PUBLIC stdc++exp)
    endif()
endif()
