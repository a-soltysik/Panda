add_library(drip_common STATIC)
add_library(drip::common ALIAS drip_common)

target_sources(
        drip_common
        PRIVATE
        src/log/Logger.cpp
        src/log/LogMessageBuilder.cpp
        src/log/sink/ConsoleSink.cpp
        src/log/sink/FileSink.cpp
        src/utils/Timer.cpp
)

target_include_directories(
        drip_common
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_system_libraries(
        drip_common
        PUBLIC
        Boost::exception
        Boost::stacktrace
        Boost::stacktrace_from_exception
        Boost::thread
        fmt::fmt
        glm::glm
)

drip_library(drip_common)
