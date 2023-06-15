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

#include "utils/Assert.h"
#include <vulkan/vulkan.hpp>
#include <GLFW/glfw3.h>
