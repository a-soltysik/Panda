#pragma once

/// @file
/// Core package entry point and version query.

#include <panda/Cube.hpp>
#include <panda/Engine.hpp>
#include <panda/ExternalBuffer.hpp>
#include <panda/Geometry.hpp>
#include <panda/OffscreenTarget.hpp>
#include <panda/Plane.hpp>
#include <panda/Projection.hpp>
#include <panda/Renderer.hpp>
#include <panda/ResourceManager.hpp>
#include <panda/Sphere.hpp>
#include <panda/SubmissionManager.hpp>
#include <panda/Transform.hpp>
#include <string_view>

namespace panda
{

/// @brief Returns the version compiled into the Panda core library.
/// This query does not initialize Vulkan or CUDA.
/// @return A view of a string literal with static lifetime.
[[nodiscard]] auto version() noexcept -> std::string_view;

}
