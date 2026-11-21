#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <glm/vec2.hpp>
#include <panda/Engine.hpp>
#include <panda/rendering/RenderView.hpp>
#include <panda/resources/PixelFormat.hpp>
#include <panda/resources/Texture.hpp>
#include <panda/submission/UploadBatch.hpp>
#include <span>
#include <utility>
#include <vector>

namespace panda::test
{
namespace detail
{
inline constexpr auto skyboxFaceColors = std::array {
    std::array<std::uint16_t, 4> {0x3c00, 0x0000, 0x0000, 0x3c00},
    std::array<std::uint16_t, 4> {0x0000, 0x3c00, 0x0000, 0x3c00},
    std::array<std::uint16_t, 4> {0x0000, 0x0000, 0x3c00, 0x3c00},
    std::array<std::uint16_t, 4> {0x3800, 0x3800, 0x0000, 0x3c00},
    std::array<std::uint16_t, 4> {0x0000, 0x3c00, 0x3c00, 0x3c00},
    std::array<std::uint16_t, 4> {0x3c00, 0x0000, 0x3c00, 0x3c00}
};

inline auto makeEnvironmentPixels(glm::uvec2 extent, std::array<std::uint16_t, 4> color) -> std::vector<std::uint16_t>
{
    auto pixels = std::vector<std::uint16_t>(static_cast<std::size_t>(extent.x) * extent.y * color.size());
    for (auto offset = std::size_t {}; offset < pixels.size(); offset += color.size())
    {
        std::ranges::copy(color, pixels.begin() + static_cast<std::ptrdiff_t>(offset));
    }
    return pixels;
}

inline auto writeSkyboxFaces(UploadBatch& batch, const Texture& skybox) -> Result<void>
{
    for (auto face = std::uint32_t {}; face < 6U; ++face)
    {
        const auto pixels = makeEnvironmentPixels({1, 1}, skyboxFaceColors.at(face));
        if (const auto written = batch.write(skybox,
                                             std::as_bytes(std::span {
                                                 pixels
        }),
                                             {.extent = {1, 1}, .arrayLayer = face});
            !written)
        {
            return std::unexpected {written.error()};
        }
    }
    return {};
}

inline auto waitForFixtureUpload(const Engine& engine, UploadBatch& batch) -> Result<void>
{
    const auto completion = batch.submit();
    if (!completion)
    {
        return std::unexpected {completion.error()};
    }
    const auto waited = engine.getSubmissionManager().wait(*completion, std::chrono::seconds {10});
    if (!waited)
    {
        return std::unexpected {waited.error()};
    }
    if (*waited != WaitStatus::Completed)
    {
        return std::unexpected {makeError(ErrorCode::Timeout, "Texture fixture upload timed out")};
    }
    return {};
}

inline void addSpecularHighlight(std::vector<std::uint16_t>& pixels, glm::uvec2 extent)
{
    for (auto row = 3U; row < 5U; ++row)
    {
        for (auto column = 3U; column < 5U; ++column)
        {
            const auto offset = ((static_cast<std::size_t>(row) * extent.x) + column) * 4U;
            pixels.at(offset) = 0x4800;
        }
    }
}

inline void addCubeSeamHighlight(std::vector<std::uint16_t>& pixels, glm::uvec2 extent, std::uint32_t face)
{
    if ((face != 4U && face != 0U) || extent.x < 2U)
    {
        return;
    }
    const auto firstColumn = face == 4U ? extent.x - 2U : 0U;
    for (auto rowIndex = 3U; rowIndex < 5U; ++rowIndex)
    {
        for (auto columnIndex = firstColumn; columnIndex < firstColumn + 2U; ++columnIndex)
        {
            const auto offset = ((static_cast<std::size_t>(rowIndex) * extent.x) + columnIndex) * 4U;
            pixels.at(offset) = 0x4800;
        }
    }
}

inline auto makeSpecularEnvironmentFace(glm::uvec2 extent, std::uint32_t face, std::uint32_t mip)
    -> std::vector<std::uint16_t>
{
    const auto radiance = face == 4U && mip > 0U ? std::array<std::uint16_t, 4> {0x3800, 0x0000, 0x0000, 0x3c00}
                                                 : std::array<std::uint16_t, 4> {0x0000, 0x0000, 0x0000, 0x3c00};
    auto pixels = makeEnvironmentPixels(extent, radiance);
    if (face == 4U && mip == 0U)
    {
        addSpecularHighlight(pixels, extent);
    }
    return pixels;
}

inline auto makeGeneratedSpecularEnvironmentFace(glm::uvec2 extent, std::uint32_t face) -> std::vector<std::uint16_t>
{
    auto pixels = makeEnvironmentPixels(extent, {0x0000, 0x0000, 0x0000, 0x3c00});
    if (face == 4U)
    {
        addSpecularHighlight(pixels, extent);
    }
    addCubeSeamHighlight(pixels, extent, face);
    return pixels;
}

inline auto writeDiffuseEnvironmentFace(UploadBatch& batch, const Texture& diffuse, std::uint32_t face) -> Result<void>
{
    const auto pixels = makeEnvironmentPixels({1, 1}, {0x0000, 0x4200, 0x0000, 0x3c00});
    return batch.write(diffuse,
                       std::as_bytes(std::span {
                           pixels
    }),
                       {.extent = {1, 1}, .arrayLayer = face});
}

inline auto writeSpecularEnvironmentFace(UploadBatch& batch, const Texture& specular, std::uint32_t face)
    -> Result<void>
{
    for (auto mip = std::uint32_t {}; mip < specular.getMipLevels(); ++mip)
    {
        const auto extent = glm::uvec2 {std::max(1U, 8U >> mip), std::max(1U, 8U >> mip)};
        const auto pixels = makeSpecularEnvironmentFace(extent, face, mip);
        if (const auto written = batch.write(specular,
                                             std::as_bytes(std::span {pixels}),
                                             {.extent = extent, .mipLevel = mip, .arrayLayer = face});
            !written)
        {
            return std::unexpected {written.error()};
        }
    }
    return {};
}

inline auto writeGeneratedSpecularEnvironmentFaces(UploadBatch& batch, const Texture& specular) -> Result<void>
{
    const auto extent = specular.getExtent();
    for (auto face = std::uint32_t {}; face < 6U; ++face)
    {
        const auto pixels = makeGeneratedSpecularEnvironmentFace(extent, face);
        if (const auto written =
                batch.write(specular, std::as_bytes(std::span {pixels}), {.extent = extent, .arrayLayer = face});
            !written)
        {
            return std::unexpected {written.error()};
        }
    }
    return {};
}

inline auto writeEnvironmentFaces(UploadBatch& batch, const Texture& diffuse, const Texture& specular) -> Result<void>
{
    for (auto face = std::uint32_t {}; face < 6U; ++face)
    {
        if (const auto written = writeDiffuseEnvironmentFace(batch, diffuse, face); !written)
        {
            return std::unexpected {written.error()};
        }
        if (const auto written = writeSpecularEnvironmentFace(batch, specular, face); !written)
        {
            return std::unexpected {written.error()};
        }
    }
    return {};
}

}

[[nodiscard]] inline auto createEnvironmentLightingFixture(const Engine& engine) -> Result<EnvironmentLighting>
{
    auto diffuse = engine.getResourceManager().createTexture({
        .extent = {1, 1},
        .format = PixelFormat::Rgba16Float,
        .usage = TextureUsage::TransferDestination | TextureUsage::Sampled,
        .type = TextureType::CubeMap
    });
    if (!diffuse)
    {
        return std::unexpected {diffuse.error()};
    }
    auto specular = engine.getResourceManager().createTexture({
        .extent = {8, 8},
        .format = PixelFormat::Rgba16Float,
        .usage = TextureUsage::TransferDestination | TextureUsage::Sampled,
        .mipPolicy = TextureMipPolicy::Supplied,
        .type = TextureType::CubeMap
    });
    if (!specular)
    {
        return std::unexpected {specular.error()};
    }
    auto batch = engine.getSubmissionManager().createUploadBatch();
    if (!batch)
    {
        return std::unexpected {batch.error()};
    }
    if (const auto written = detail::writeEnvironmentFaces(*batch, *diffuse, *specular); !written)
    {
        return std::unexpected {written.error()};
    }
    if (const auto uploaded = detail::waitForFixtureUpload(engine, *batch); !uploaded)
    {
        return std::unexpected {uploaded.error()};
    }
    return EnvironmentLighting {.diffuseIrradiance = std::move(*diffuse), .specularRadiance = std::move(*specular)};
}

[[nodiscard]] inline auto createGeneratedSpecularEnvironmentFixture(const Engine& engine, std::uint32_t samplesPerTexel)
    -> Result<Texture>
{
    auto specular = engine.getResourceManager().createTexture({
        .extent = {8, 8},
        .format = PixelFormat::Rgba16Float,
        .usage = TextureUsage::TransferDestination | TextureUsage::Sampled,
        .mipPolicy = TextureMipPolicy::GenerateSpecularEnvironment,
        .specularEnvironmentSamplesPerTexel = samplesPerTexel,
        .type = TextureType::CubeMap
    });
    if (!specular)
    {
        return std::unexpected {specular.error()};
    }
    auto batch = engine.getSubmissionManager().createUploadBatch();
    if (!batch)
    {
        return std::unexpected {batch.error()};
    }
    if (const auto written = detail::writeGeneratedSpecularEnvironmentFaces(*batch, *specular); !written)
    {
        return std::unexpected {written.error()};
    }
    if (const auto uploaded = detail::waitForFixtureUpload(engine, *batch); !uploaded)
    {
        return std::unexpected {uploaded.error()};
    }
    return std::move(*specular);
}

[[nodiscard]] inline auto createSkyboxFixture(const Engine& engine) -> Result<Texture>
{
    auto skybox = engine.getResourceManager().createTexture({
        .extent = {1, 1},
        .format = PixelFormat::Rgba16Float,
        .usage = TextureUsage::TransferDestination | TextureUsage::Sampled,
        .type = TextureType::CubeMap
    });
    if (!skybox)
    {
        return std::unexpected {skybox.error()};
    }
    auto batch = engine.getSubmissionManager().createUploadBatch();
    if (!batch)
    {
        return std::unexpected {batch.error()};
    }
    if (const auto written = detail::writeSkyboxFaces(*batch, *skybox); !written)
    {
        return std::unexpected {written.error()};
    }
    if (const auto uploaded = detail::waitForFixtureUpload(engine, *batch); !uploaded)
    {
        return std::unexpected {uploaded.error()};
    }
    return std::move(*skybox);
}
}
