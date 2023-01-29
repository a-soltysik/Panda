cmake_minimum_required(VERSION 3.22)
project(Panda)

set(CMAKE_CXX_STANDARD 20)

if (GNU)
    set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} -Wall -Wextra -Wshadow -Wpessimizing-move -Wredundant-move -Wpedantic -Werror -ffast-math)
    set(CMAKE_CXX_FLAGS_DEBUG ${CMAKE_CXX_FLAGS_DEBUG} -Og)
    set(CMAKE_CXX_FLAGS_RELEASE ${CMAKE_CXX_FLAGS_RELEASE} -O3 -flto)
elseif(MSVC)
endif()


set(SRC_DIR ${PROJECT_SOURCE_DIR}/src)
set(EXT_DIR ${PROJECT_SOURCE_DIR}/ext)

set(GLM_DIR ${EXT_DIR}/glm)
set(GLFW_DIR ${EXT_DIR}/glfw)

add_subdirectory(${GLM_DIR})
add_subdirectory(${GLFW_DIR})
add_subdirectory(${SRC_DIR})