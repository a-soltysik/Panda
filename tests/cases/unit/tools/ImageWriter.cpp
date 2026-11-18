#define STB_IMAGE_IMPLEMENTATION
#include <gtest/gtest.h>
#include <stb_image.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <limits>
#include <memory>
#include <panda/base/Error.hpp>
#include <panda/resources/OffscreenTarget.hpp>
#include <panda/tools/image/Writer.hpp>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace
{
using panda::ImageReadback;
using panda::tools::image::writeBmp;
using panda::tools::image::writeJpeg;
using panda::tools::image::writePng;
using panda::tools::image::writeRawRgba;
constexpr auto rgbaChannels = std::size_t {4};
constexpr auto imageSide = std::size_t {8};
constexpr auto maximumJpegQuality = std::uint8_t {100};
constexpr auto bitmapBitsPerPixelOffset = std::streamoff {28};
constexpr auto bitmap24BitsPerPixel = std::array<char, 2> {static_cast<char>(24), 0};

class TemporaryImage
{
public:
    explicit TemporaryImage(std::string_view extension)
    {
        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        _path =
            std::filesystem::temp_directory_path() / ("panda-image-" + std::to_string(ticks) + std::string {extension});
    }

    TemporaryImage(const TemporaryImage&) = delete;

    auto operator=(const TemporaryImage&) -> TemporaryImage& = delete;
    TemporaryImage(TemporaryImage&&) = delete;
    auto operator=(TemporaryImage&&) -> TemporaryImage& = delete;

    ~TemporaryImage()
    {
        std::error_code ignored;
        std::filesystem::remove(_path, ignored);
    }

    [[nodiscard]] auto getPath() const noexcept -> const std::filesystem::path&
    {
        return _path;
    }

private:
    std::filesystem::path _path;
};

using ImagePixels = std::unique_ptr<stbi_uc, decltype(&stbi_image_free)>;

struct DecodedImage
{
    ImagePixels pixels {nullptr, &stbi_image_free};
    int width {};
    int height {};
};

struct RgbaPixel
{
    stbi_uc red {};
    stbi_uc green {};
    stbi_uc blue {};
    stbi_uc alpha {};
};

auto rgbaPixels(const DecodedImage& image) -> std::span<const stbi_uc>
{
    const auto pixelCount = static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height);
    return {image.pixels.get(), pixelCount * rgbaChannels};
}

auto centerPixel(const DecodedImage& image) -> RgbaPixel
{
    const auto width = static_cast<std::size_t>(image.width);
    const auto height = static_cast<std::size_t>(image.height);
    const auto center = ((height / 2 * width) + (width / 2)) * rgbaChannels;
    auto channels = std::array<stbi_uc, rgbaChannels> {};
    std::ranges::copy(rgbaPixels(image).subspan(center, rgbaChannels), channels.begin());
    return {
        .red = channels.at(0),
        .green = channels.at(1),
        .blue = channels.at(2),
        .alpha = channels.at(3),
    };
}

auto decode(const std::filesystem::path& path) -> DecodedImage
{
    auto image = DecodedImage {};
    const auto pathText = path.string();
    image.pixels.reset(stbi_load(pathText.c_str(), &image.width, &image.height, nullptr, 4));
    return image;
}

auto translucentImage() -> ImageReadback
{
    constexpr auto pixel = std::array<std::uint8_t, 4> {128, 64, 255, 128};
    auto image = ImageReadback {
        .extent = {8, 8},
        .rgba = {}
    };
    image.rgba.reserve(imageSide * imageSide * pixel.size());
    for (auto index = std::size_t {}; index < imageSide * imageSide; ++index)
    {
        image.rgba.insert(image.rgba.end(), pixel.begin(), pixel.end());
    }
    return image;
}
}

TEST(ImageWriter, WritesRawPixelsWithoutAddingMetadata)
{
    const auto output = TemporaryImage {".rgba"};
    const auto image = ImageReadback {
        .extent = {1, 2},
        .rgba = {255, 0, 0, 0, 0, 255, 0, 128}
    };
    ASSERT_TRUE(writeRawRgba(image, output.getPath()));
    auto input = std::ifstream {output.getPath(), std::ios::binary};
    const auto bytes =
        std::vector<std::uint8_t> {std::istreambuf_iterator<char> {input}, std::istreambuf_iterator<char> {}};
    EXPECT_EQ(bytes, image.rgba);
}

TEST(ImageWriter, WritesTopDownRgbaPngAndPreservesAlpha)
{
    const auto output = TemporaryImage {".png"};
    const auto image = ImageReadback {
        .extent = {1, 2},
        .rgba = {255, 0, 0, 0, 0, 255, 0, 128}
    };
    ASSERT_TRUE(writePng(image, output.getPath()));

    const auto decoded = decode(output.getPath());
    ASSERT_NE(decoded.pixels, nullptr);
    ASSERT_EQ(decoded.width, 1);
    ASSERT_EQ(decoded.height, 2);
    const auto expected = std::array<stbi_uc, 8> {255, 0, 0, 0, 0, 255, 0, 128};
    EXPECT_TRUE(std::ranges::equal(rgbaPixels(decoded).first(expected.size()), expected));
}

TEST(ImageWriter, WritesTwentyFourBitBitmapAndCompositesAlphaOverBlack)
{
    const auto output = TemporaryImage {".bmp"};
    const auto image = translucentImage();
    ASSERT_TRUE(writeBmp(image, output.getPath()));

    auto header = std::ifstream {output.getPath(), std::ios::binary};
    ASSERT_TRUE(header);
    header.seekg(bitmapBitsPerPixelOffset);
    auto bitsPerPixel = std::array<char, 2> {};
    ASSERT_TRUE(header.read(bitsPerPixel.data(), static_cast<std::streamsize>(bitsPerPixel.size())));
    EXPECT_EQ(bitsPerPixel, bitmap24BitsPerPixel);

    const auto decoded = decode(output.getPath());
    ASSERT_NE(decoded.pixels, nullptr);
    ASSERT_EQ(decoded.width, 8);
    ASSERT_EQ(decoded.height, 8);
    const auto center = centerPixel(decoded);
    EXPECT_NEAR(center.red, 92, 2);
    EXPECT_NEAR(center.green, 44, 2);
    EXPECT_NEAR(center.blue, 188, 2);
    EXPECT_EQ(center.alpha, 255);
}

TEST(ImageWriter, WritesJpegWithValidatedQualityAndLinearAlphaComposite)
{
    const auto output = TemporaryImage {".jpg"};
    const auto image = translucentImage();
    ASSERT_TRUE(writeJpeg(image, output.getPath(), maximumJpegQuality));

    const auto decoded = decode(output.getPath());
    ASSERT_NE(decoded.pixels, nullptr);
    ASSERT_EQ(decoded.width, 8);
    ASSERT_EQ(decoded.height, 8);
    const auto center = centerPixel(decoded);
    EXPECT_NEAR(center.red, 92, 5);
    EXPECT_NEAR(center.green, 44, 5);
    EXPECT_NEAR(center.blue, 188, 5);
    EXPECT_EQ(center.alpha, 255);
}

TEST(ImageWriter, PreservesOpaqueLowSrgbValuesInBitmap)
{
    const auto output = TemporaryImage {".bmp"};
    const auto image = ImageReadback {
        .extent = {2, 1},
        .rgba = {1, 4, 0, 255, 128, 64, 255, 128}
    };
    ASSERT_TRUE(writeBmp(image, output.getPath()));

    const auto decoded = decode(output.getPath());
    ASSERT_NE(decoded.pixels, nullptr);
    const auto firstPixel = rgbaPixels(decoded).first(rgbaChannels);
    static constexpr auto expected = std::array<stbi_uc, 4> {1, 4, 0, 255};
    EXPECT_TRUE(std::ranges::equal(firstPixel, expected));
}

TEST(ImageWriter, RejectsInvalidPixelsAndJpegQualityBeforeOpeningOutput)
{
    const auto output = TemporaryImage {".jpg"};
    const auto empty = ImageReadback {};
    const auto malformed = ImageReadback {
        .extent = {1, 1},
        .rgba = {1, 2, 3}
    };
    const auto oversized = ImageReadback {
        .extent = {std::numeric_limits<std::uint32_t>::max(), 1},
        .rgba = {}
    };
    const auto oversizedHeight = ImageReadback {
        .extent = {1, std::numeric_limits<std::uint32_t>::max()},
        .rgba = {}
    };
    const auto tooManyPixels = ImageReadback {
        .extent = {32768, 32768},
        .rgba = {}
    };
    const auto valid = translucentImage();

    const auto invalidRaw = writeRawRgba(empty, output.getPath());
    ASSERT_FALSE(invalidRaw);
    EXPECT_EQ(invalidRaw.error().code, panda::ErrorCode::InvalidArgument);
    const auto zeroExtent = writePng(empty, output.getPath());
    ASSERT_FALSE(zeroExtent);
    EXPECT_EQ(zeroExtent.error().code, panda::ErrorCode::InvalidArgument);
    const auto wrongByteCount = writeBmp(malformed, output.getPath());
    ASSERT_FALSE(wrongByteCount);
    EXPECT_EQ(wrongByteCount.error().code, panda::ErrorCode::InvalidArgument);
    const auto unsupportedExtent = writePng(oversized, output.getPath());
    ASSERT_FALSE(unsupportedExtent);
    EXPECT_EQ(unsupportedExtent.error().code, panda::ErrorCode::InvalidArgument);
    const auto unsupportedHeight = writePng(oversizedHeight, output.getPath());
    ASSERT_FALSE(unsupportedHeight);
    EXPECT_EQ(unsupportedHeight.error().code, panda::ErrorCode::InvalidArgument);
    const auto excessivePixelCount = writePng(tooManyPixels, output.getPath());
    ASSERT_FALSE(excessivePixelCount);
    EXPECT_EQ(excessivePixelCount.error().code, panda::ErrorCode::InvalidArgument);
    const auto invalidJpeg = writeJpeg(empty, output.getPath());
    ASSERT_FALSE(invalidJpeg);
    EXPECT_EQ(invalidJpeg.error().code, panda::ErrorCode::InvalidArgument);
    const auto lowQuality = writeJpeg(valid, output.getPath(), 0);
    ASSERT_FALSE(lowQuality);
    EXPECT_EQ(lowQuality.error().code, panda::ErrorCode::InvalidArgument);
    const auto highQuality = writeJpeg(valid, output.getPath(), static_cast<std::uint8_t>(maximumJpegQuality + 1));
    ASSERT_FALSE(highQuality);
    EXPECT_EQ(highQuality.error().code, panda::ErrorCode::InvalidArgument);
    EXPECT_FALSE(std::filesystem::exists(output.getPath()));
}

TEST(ImageWriter, ReturnsFileErrorsWithoutRequiringGpuHardware)
{
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto output =
        std::filesystem::temp_directory_path() / ("panda-image-missing-" + std::to_string(ticks)) / "image.png";
    const auto image = translucentImage();
    const auto rawResult = writeRawRgba(image, output);
    ASSERT_FALSE(rawResult);
    EXPECT_EQ(rawResult.error().code, panda::ErrorCode::BackendFailure);
    const auto pngResult = writePng(image, output);
    ASSERT_FALSE(pngResult);
    EXPECT_EQ(pngResult.error().code, panda::ErrorCode::BackendFailure);
    const auto bmpResult = writeBmp(image, output);
    ASSERT_FALSE(bmpResult);
    EXPECT_EQ(bmpResult.error().code, panda::ErrorCode::BackendFailure);
    const auto jpegResult = writeJpeg(image, output);
    ASSERT_FALSE(jpegResult);
    EXPECT_EQ(jpegResult.error().code, panda::ErrorCode::BackendFailure);
}

TEST(ImageWriter, ReturnsWriteErrorsWhenTheOutputDeviceIsFull)
{
    const auto output = std::filesystem::path {"/dev/full"};
    if (!std::filesystem::exists(output))
    {
        GTEST_SKIP() << "The full output device is unavailable on this platform";
    }

    const auto image = translucentImage();
    const auto rawResult = writeRawRgba(image, output);
    ASSERT_FALSE(rawResult);
    EXPECT_EQ(rawResult.error().code, panda::ErrorCode::BackendFailure);
    const auto pngResult = writePng(image, output);
    ASSERT_FALSE(pngResult);
    EXPECT_EQ(pngResult.error().code, panda::ErrorCode::BackendFailure);
    const auto bmpResult = writeBmp(image, output);
    ASSERT_FALSE(bmpResult);
    EXPECT_EQ(bmpResult.error().code, panda::ErrorCode::BackendFailure);
    const auto jpegResult = writeJpeg(image, output);
    ASSERT_FALSE(jpegResult);
    EXPECT_EQ(jpegResult.error().code, panda::ErrorCode::BackendFailure);
}
