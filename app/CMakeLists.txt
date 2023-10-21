set(APP_TARGET_NAME ${PROJECT_NAME}.App)
set(RESOURCE_BIN_DIR ${CMAKE_CURRENT_BINARY_DIR}/res)
set(RESOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/res)
set(CONFIG_OUTPUT ${CMAKE_BINARY_DIR}/app/generated)

add_subdirectory(config)

file(GLOB_RECURSE ${APP_TARGET_NAME}_SRC_LIST
     ${APP_DIR}/*.h
     ${APP_DIR}/*.cpp
)

add_executable(${APP_TARGET_NAME} ${${APP_TARGET_NAME}_SRC_LIST})

target_link_libraries(${APP_TARGET_NAME} PRIVATE PD::PD_options PD::PD_warnings)

target_include_directories(${APP_TARGET_NAME} PRIVATE ${CMAKE_CURRENT_LIST_DIR} ${CONFIG_OUTPUT})

if (PD_ENABLE_PCH)
    target_precompile_headers(${APP_TARGET_NAME} PRIVATE Common.h)
endif ()

find_package(Vulkan REQUIRED)
target_link_system_libraries(
    ${APP_TARGET_NAME}
    PRIVATE
    Vulkan::Vulkan glfw glm fmt imgui implot)

target_link_libraries(
    ${APP_TARGET_NAME}
    PRIVATE
    Panda.Engine)

message(STATUS "Configuring resource directory")
add_custom_command(TARGET ${APP_TARGET_NAME} PRE_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy_directory
                   ${RESOURCE_DIR} ${RESOURCE_BIN_DIR})

add_custom_target(run COMMAND ${APP_TARGET_NAME} DEPENDS ${APP_TARGET_NAME})
