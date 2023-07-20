#pragma once

#define PD_MOVE_ONLY(className)                            \
    className(const className&) = delete;                  \
    className(className&&) = default;                      \
    auto operator=(const className&)->className& = delete; \
    auto operator=(className&&)->className& = default

#define PD_COPY_ONLY(className)                             \
    className(const className&) = default;                  \
    className(className&&) = delete;                        \
    auto operator=(const className&)->className& = default; \
    auto operator=(className&&)->className& = delete

#define DEFAULT_ALL(className)                              \
    className(const className&) = default;                  \
    className(className&&) = default;                       \
    auto operator=(const className&)->className& = default; \
    auto operator=(className&&)->className& = default

#define PD_DELETE_ALL(className)                           \
    className(const className&) = delete;                  \
    className(className&&) = delete;                       \
    auto operator=(const className&)->className& = delete; \
    auto operator=(className&&)->className& = delete

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL

#include <glm/glm.hpp>
#include <glm/ext.hpp>
#include <glm/gtx/hash.hpp>

#include "utils/Assert.h"
#include <vulkan/vulkan.hpp>
#include <GLFW/glfw3.h>
