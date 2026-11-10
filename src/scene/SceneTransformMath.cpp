#include "SceneTransformMath.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <expected>
#include <glm/ext/matrix_float3x3.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/quaternion_double.hpp>
#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/quaternion_geometric.hpp>
#include <glm/ext/vector_double3.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/matrix.hpp>
#include <limits>
#include <panda/Error.hpp>
#include <panda/Transform.hpp>
#include <utility>

#include "Math.hpp"

namespace panda::scene::detail
{
using ::panda::detail::isFinite;

namespace
{
constexpr auto minimumRotationLength = 1.0e-8;
constexpr auto orthogonalityTolerance = 1.0e-5F;
constexpr auto reconstructionTolerance = 1.0e-5F;
constexpr auto inverseTolerance = 1.0e-4F;
constexpr auto basisDeterminantTolerance = 1.0e-8F;

struct Basis
{
    glm::vec3 scale;
    glm::quat rotation;
};

auto normalizeRotation(glm::quat rotation) -> glm::quat
{
    const auto normalized = glm::normalize(glm::dquat {static_cast<double>(rotation.w),
                                                       static_cast<double>(rotation.x),
                                                       static_cast<double>(rotation.y),
                                                       static_cast<double>(rotation.z)});
    return {static_cast<float>(normalized.w),
            static_cast<float>(normalized.x),
            static_cast<float>(normalized.y),
            static_cast<float>(normalized.z)};
}

auto maximumMagnitude(const glm::mat4& matrix) -> float
{
    auto result = 0.0F;
    for (auto column = 0; column < 4; ++column)
    {
        for (auto row = 0; row < 4; ++row)
        {
            result = std::max(result, std::abs(matrix[column][row]));
        }
    }
    return result;
}

auto vectorLength(glm::vec3 vector) -> double
{
    return glm::length(glm::dvec3 {vector});
}

auto basisScale(const glm::mat4& matrix) -> Result<glm::vec3>
{
    const auto lengths = std::array {vectorLength(glm::vec3 {matrix[0]}),
                                     vectorLength(glm::vec3 {matrix[1]}),
                                     vectorLength(glm::vec3 {matrix[2]})};
    if (!isFinite(matrix) || std::ranges::any_of(lengths, [](double length) {
            return length == 0.0 || length > static_cast<double>(std::numeric_limits<float>::max());
        }))
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "World transform is singular or non-finite")};
    }
    return glm::vec3 {static_cast<float>(lengths[0]), static_cast<float>(lengths[1]), static_cast<float>(lengths[2])};
}

auto orthonormalBasis(const glm::mat4& matrix, glm::vec3& scale) -> Result<glm::mat3>
{
    auto x = glm::vec3 {matrix[0]} / scale.x;
    auto y = glm::vec3 {matrix[1]} / scale.y;
    auto z = glm::vec3 {matrix[2]} / scale.z;
    if (std::abs(glm::dot(x, y)) > orthogonalityTolerance || std::abs(glm::dot(x, z)) > orthogonalityTolerance ||
        std::abs(glm::dot(y, z)) > orthogonalityTolerance)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "World transform requires unsupported shear")};
    }
    if (glm::dot(x, glm::cross(y, z)) < 0.0F)
    {
        scale.x = -scale.x;
        x = -x;
    }
    auto rotationMatrix = glm::mat3 {1.0F};
    rotationMatrix[0] = x;
    rotationMatrix[1] = y;
    rotationMatrix[2] = z;
    return rotationMatrix;
}

auto decomposeBasis(const glm::mat4& matrix) -> Result<Basis>
{
    auto scale = basisScale(matrix);
    if (!scale)
    {
        return std::unexpected {scale.error()};
    }
    auto rotationMatrix = orthonormalBasis(matrix, *scale);
    if (!rotationMatrix)
    {
        return std::unexpected {rotationMatrix.error()};
    }
    const auto rotation = glm::quat_cast(*rotationMatrix);
    const auto rotationLength = vectorLength(glm::vec3 {rotation.x, rotation.y, rotation.z});
    const auto fullRotationLength = std::hypot(rotationLength, static_cast<double>(rotation.w));
    if (!std::isfinite(fullRotationLength) || fullRotationLength < minimumRotationLength)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "World transform rotation is not representable")};
    }
    return Basis {.scale = *scale, .rotation = normalizeRotation(rotation)};
}
}

auto validateTransform(const Transform& transform) -> Result<std::pair<Transform, glm::mat4>>
{
    auto matrix = createModelMatrix(transform);
    if (!matrix)
    {
        return std::unexpected {matrix.error()};
    }
    auto normalized = transform;
    normalized.rotation = normalizeRotation(transform.rotation);
    return std::pair {normalized, *matrix};
}

auto decomposeTransform(const glm::mat4& matrix) -> Result<std::pair<Transform, glm::mat4>>
{
    auto basis = decomposeBasis(matrix);
    if (!basis)
    {
        return std::unexpected {basis.error()};
    }
    auto transform = Transform {.position = glm::vec3 {matrix[3]}, .rotation = basis->rotation, .scale = basis->scale};
    auto reconstructed = createModelMatrix(transform);
    if (!reconstructed)
    {
        return std::unexpected {reconstructed.error()};
    }
    const auto magnitude = maximumMagnitude(matrix);
    if (magnitude == 0.0F || maximumMagnitude(matrix - *reconstructed) > reconstructionTolerance * magnitude)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "World transform cannot be represented as TRS")};
    }
    return std::pair {transform, *reconstructed};
}

auto checkedInverse(const glm::mat4& matrix) -> Result<glm::mat4>
{
    auto x = glm::vec3 {matrix[0]};
    auto y = glm::vec3 {matrix[1]};
    auto z = glm::vec3 {matrix[2]};
    const auto xLength = vectorLength(x);
    const auto yLength = vectorLength(y);
    const auto zLength = vectorLength(z);
    if (!isFinite(matrix) || xLength == 0.0 || yLength == 0.0 || zLength == 0.0)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Parent world transform is singular")};
    }
    x /= static_cast<float>(xLength);
    y /= static_cast<float>(yLength);
    z /= static_cast<float>(zLength);
    if (std::abs(glm::dot(x, glm::cross(y, z))) <= basisDeterminantTolerance)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Parent world transform is singular")};
    }
    const auto inverse = glm::inverse(matrix);
    if (!isFinite(inverse) || maximumMagnitude(matrix * inverse - glm::mat4 {1.0F}) > inverseTolerance)
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "Parent world transform has no stable inverse")};
    }
    return inverse;
}

auto extractPoseRotation(const glm::mat4& world) -> Result<glm::quat>
{
    auto basis = decomposeBasis(world);
    if (!basis)
    {
        return std::unexpected {basis.error()};
    }
    const auto scale = basis->scale;
    const auto tolerance = 1.0e-5F * std::max({scale.x, scale.y, scale.z});
    if (scale.x <= 0.0F || std::abs(scale.x - scale.y) > tolerance || std::abs(scale.x - scale.z) > tolerance)
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Camera and light require positive uniform world scale")};
    }
    return basis->rotation;
}
}
