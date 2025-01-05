cmake_minimum_required(VERSION 3.16)
project(implot CXX)

set(CMAKE_DEBUG_POSTFIX d)

if (APPLE)
    set(CMAKE_CXX_STANDARD 11)
    enable_language(OBJCXX)
endif ()

CPMAddPackage("gh:epezent/implot@0.16")

add_library(${PROJECT_NAME})
add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})
target_include_directories(
        ${PROJECT_NAME}
        PUBLIC
        $<BUILD_INTERFACE:${implot_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_sources(
        ${PROJECT_NAME}
        PRIVATE
        ${implot_SOURCE_DIR}/implot.cpp
        ${implot_SOURCE_DIR}/implot_demo.cpp
        ${implot_SOURCE_DIR}/implot_items.cpp
)

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_11)
target_link_libraries(${PROJECT_NAME} PRIVATE imgui)

install(
        TARGETS ${PROJECT_NAME}
        EXPORT ${PROJECT_NAME}_target
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin
)

install(FILES
        ${implot_SOURCE_DIR}/implot.h
        ${implot_SOURCE_DIR}/implot_internal.h
        DESTINATION include
)

install(
        EXPORT ${PROJECT_NAME}_target
        NAMESPACE ${PROJECT_NAME}::
        FILE ${PROJECT_NAME}-targets.cmake
        DESTINATION share/${PROJECT_NAME}
)