#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <glm/ext/vector_float3.hpp>
#include <limits>
#include <numbers>
#include <panda/Error.hpp>
#include <panda/Geometry.hpp>
#include <panda/Sphere.hpp>
#include <variant>
#include <vector>

namespace panda
{
namespace
{
constexpr auto indicesPerQuad = std::uint64_t {6};
constexpr auto sphereRadius = 0.5F;

struct SphereCounts
{
    std::uint64_t longitudeSegments;
    std::uint64_t latitudeRings;
    std::uint64_t vertexStride;
    std::uint64_t vertexCount;
    std::uint64_t indexCount;
};

auto calculateSphereCounts(const SphereGeometryOptions& options) -> Result<SphereCounts>
{
    const auto longitudeSegments = static_cast<std::uint64_t>(options.longitudeSegments);
    const auto latitudeRings = static_cast<std::uint64_t>(options.latitudeRings);
    constexpr auto maximumMeshCount = static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max());
    if (longitudeSegments < 3 || latitudeRings < 2 || longitudeSegments + 1 > maximumMeshCount / (latitudeRings + 1))
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument,
                                          "Sphere tessellation has an invalid or unrepresentable vertex count")};
    }
    const auto vertexCount = (longitudeSegments + 1) * (latitudeRings + 1);
    const auto triangleCount = longitudeSegments * (latitudeRings - 1);
    if (triangleCount > maximumMeshCount / indicesPerQuad)
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Sphere tessellation has an unrepresentable index count")};
    }
    return SphereCounts {.longitudeSegments = longitudeSegments,
                         .latitudeRings = latitudeRings,
                         .vertexStride = longitudeSegments + 1,
                         .vertexCount = vertexCount,
                         .indexCount = triangleCount * indicesPerQuad};
}

auto makeSphereVertex(std::uint64_t ring, std::uint64_t segment, const SphereCounts& counts) -> MeshVertex
{
    const auto latitude =
        std::numbers::pi_v<double> * static_cast<double>(ring) / static_cast<double>(counts.latitudeRings);
    const auto longitude =
        2.0 * std::numbers::pi_v<double> * static_cast<double>(segment) / static_cast<double>(counts.longitudeSegments);
    const auto y = std::cos(latitude);
    const auto radial = std::sin(latitude);
    const auto cosine = std::cos(longitude);
    const auto sine = std::sin(longitude);
    auto normal =
        glm::vec3 {static_cast<float>(radial * cosine), static_cast<float>(y), static_cast<float>(radial * sine)};
    if (ring == 0)
    {
        normal = {0.0F, 1.0F, 0.0F};
    }
    else if (ring == counts.latitudeRings)
    {
        normal = {0.0F, -1.0F, 0.0F};
    }
    return MeshVertex {
        .position = sphereRadius * normal,
        .normal = normal,
        .uv = {static_cast<float>(segment) / static_cast<float>(counts.longitudeSegments),
               static_cast<float>(ring) / static_cast<float>(counts.latitudeRings)},
        .tangent = {static_cast<float>(-sine), 0.0F, static_cast<float>(cosine), 1.0F}
    };
}

void appendVertices(Geometry& geometry, const SphereCounts& counts)
{
    geometry.vertices.reserve(static_cast<std::size_t>(counts.vertexCount));
    for (auto ring = std::uint64_t {}; ring <= counts.latitudeRings; ++ring)
    {
        for (auto segment = std::uint64_t {}; segment <= counts.longitudeSegments; ++segment)
        {
            geometry.vertices.push_back(makeSphereVertex(ring, segment, counts));
        }
    }
}

template <typename Index>
void appendSegmentIndices(std::vector<Index>& indices,
                          std::uint64_t ring,
                          std::uint64_t segment,
                          const SphereCounts& counts)
{
    const auto first = static_cast<Index>((ring * counts.vertexStride) + segment);
    const auto nextLongitude = static_cast<Index>(first + 1);
    const auto nextRing = static_cast<Index>(first + counts.vertexStride);
    const auto nextRingLongitude = static_cast<Index>(nextRing + 1);
    if (ring != 0)
    {
        indices.insert(indices.end(), {first, nextLongitude, nextRing});
    }
    if (ring + 1 != counts.latitudeRings)
    {
        indices.insert(indices.end(), {nextLongitude, nextRingLongitude, nextRing});
    }
}

template <typename Index>
void appendSphereIndices(std::vector<Index>& indices, const SphereCounts& counts)
{
    indices.reserve(static_cast<std::size_t>(counts.indexCount));
    for (auto ring = std::uint64_t {}; ring < counts.latitudeRings; ++ring)
    {
        for (auto segment = std::uint64_t {}; segment < counts.longitudeSegments; ++segment)
        {
            appendSegmentIndices(indices, ring, segment, counts);
        }
    }
}

void appendIndices(Geometry& geometry, const SphereCounts& counts)
{
    constexpr auto maximum16BitVertexIndex = static_cast<std::uint64_t>(std::numeric_limits<std::uint16_t>::max());
    if (counts.vertexCount <= maximum16BitVertexIndex + 1)
    {
        geometry.indices = std::vector<std::uint16_t> {};
        appendSphereIndices(std::get<std::vector<std::uint16_t>>(geometry.indices), counts);
        return;
    }
    geometry.indices = std::vector<std::uint32_t> {};
    appendSphereIndices(std::get<std::vector<std::uint32_t>>(geometry.indices), counts);
}
}

auto createSphereGeometry(const SphereGeometryOptions& options) -> Result<Geometry>
{
    auto counts = calculateSphereCounts(options);
    if (!counts)
    {
        return std::unexpected {counts.error()};
    }

    auto geometry = Geometry {};
    appendVertices(geometry, *counts);
    appendIndices(geometry, *counts);
    return geometry;
}
}
