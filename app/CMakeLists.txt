set(APP_TARGET_NAME ${PROJECT_NAME}.App)
set(RESOURCE_DIR res)
set(RESOURCE_DATA_DIR ${CMAKE_CURRENT_LIST_DIR}/${RESOURCE_DIR})
set(RESOURCE_BIN_DIR ${CMAKE_CURRENT_BINARY_DIR}/${RESOURCE_DIR})
set(CONFIG_OUTPUT ${CMAKE_BINARY_DIR}/app/generated)

add_subdirectory(config)

file(GLOB_RECURSE ${APP_TARGET_NAME}_SRC_LIST
        ${APP_DIR}/*.cpp
)

file(GLOB_RECURSE BACKED_SRC_LIST
        ${APP_DIR}/backend/*.cpp
)

list(REMOVE_ITEM ${APP_TARGET_NAME}_SRC_LIST ${BACKED_SRC_LIST})

if (WIN32)
    file(GLOB_RECURSE WIN_SRC_LIST
            ${APP_DIR}/backend/windows/*.cpp
    )

    list(APPEND ${APP_TARGET_NAME}_SRC_LIST ${WIN_SRC_LIST})
elseif (LINUX)
    file(GLOB_RECURSE LINUX_SRC_LIST
            ${APP_DIR}/backend/linux/*.cpp
    )

    list(APPEND ${APP_TARGET_NAME}_SRC_LIST ${LINUX_SRC_LIST})
else ()
    file(GLOB_RECURSE OTHER_SRC_LIST
            ${APP_DIR}/backend/other/*.cpp
    )

    list(APPEND ${APP_TARGET_NAME}_SRC_LIST ${OTHER_SRC_LIST})
endif ()

add_executable(${APP_TARGET_NAME} ${${APP_TARGET_NAME}_SRC_LIST})

target_link_libraries(${APP_TARGET_NAME} PRIVATE PD::PD_options PD::PD_warnings)

target_include_directories(${APP_TARGET_NAME} PRIVATE ${CMAKE_CURRENT_LIST_DIR} ${CONFIG_OUTPUT})
if (MSVC)
    target_compile_definitions(${APP_TARGET_NAME} PRIVATE NOMINMAX)
endif ()

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

add_custom_command(TARGET ${APP_TARGET_NAME} PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${RESOURCE_DATA_DIR} ${RESOURCE_BIN_DIR})
