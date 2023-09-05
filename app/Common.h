#pragma once

#ifndef PD_MOVE_ONLY
#    define PD_MOVE_ONLY(className)                            \
        className(const className&) = delete;                  \
        className(className&&) = default;                      \
        auto operator=(const className&)->className& = delete; \
        auto operator=(className&&)->className& = default
#endif

#ifndef PD_COPY_ONLY
#    define PD_COPY_ONLY(className)                             \
        className(const className&) = default;                  \
        className(className&&) = delete;                        \
        auto operator=(const className&)->className& = default; \
        auto operator=(className&&)->className& = delete
#endif

#ifndef PD_DEFAULT_ALL
#    define PD_DEFAULT_ALL(className)                           \
        className(const className&) = default;                  \
        className(className&&) = default;                       \
        auto operator=(const className&)->className& = default; \
        auto operator=(className&&)->className& = default
#endif

#ifndef PD_DELETE_ALL
#    define PD_DELETE_ALL(className)                           \
        className(const className&) = delete;                  \
        className(className&&) = delete;                       \
        auto operator=(const className&)->className& = delete; \
        auto operator=(className&&)->className& = delete
#endif

#include <panda/utils/Assert.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL

#include <glm/ext.hpp>
#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
