#pragma once

/// @file
/// Core package entry point and version query.

#include <panda/Engine.hpp>
#include <panda/geometry/Cube.hpp>
#include <panda/geometry/Geometry.hpp>
#include <panda/geometry/Plane.hpp>
#include <panda/geometry/Sphere.hpp>
#include <panda/interop/ExternalBuffer.hpp>
#include <panda/math/Projection.hpp>
#include <panda/math/Transform.hpp>
#include <panda/rendering/FrameResult.hpp>
#include <panda/rendering/OffscreenRenderer.hpp>
#include <panda/rendering/WindowRenderer.hpp>
#include <panda/resources/Buffer.hpp>
#include <panda/resources/OffscreenTarget.hpp>
#include <panda/resources/PixelFormat.hpp>
#include <panda/resources/ResourceManager.hpp>
#include <panda/resources/Texture.hpp>
#include <panda/submission/SubmissionManager.hpp>
#include <string_view>

namespace panda
{

/// @brief Returns the version compiled into the Panda core library.
/// This query does not initialize Vulkan or CUDA.
/// @return A view of a string literal with static lifetime.
[[nodiscard]] auto version() noexcept -> std::string_view;

}
