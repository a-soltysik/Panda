// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
// clang-format on

#include "TextureResource.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <vulkan/vulkan_core.h>

#include <array>
#include <bit>
#include <cstdint>
#include <expected>
#include <memory>
#include <panda/base/Error.hpp>
#include <panda/diagnostics/Assert.hpp>
#include <panda/resources/PixelFormat.hpp>
#include <panda/resources/Texture.hpp>
#include <tuple>
#include <utility>

#include "FakeHandle.hpp"
#include "ResourceMappings.hpp"
#include "ScopedMock.hpp"
#include "external/vulkan/VulkanMock.hpp"
#include "panda/core/MemoryAllocationMock.hpp"
#include "panda/core/MemoryPoolMock.hpp"
#include "resources/memory/MemoryPool.hpp"
#include "resources/memory/RangeAllocator.hpp"

namespace
{
using panda::core::vulkan::resources::MemoryPool;
using panda::detail::TextureResource;
using panda::test::fakeHandle;
using testing::_;
using testing::NotNull;
using testing::Ref;
using testing::Return;

constexpr auto success = static_cast<VkResult>(vk::Result::eSuccess);
constexpr auto exhausted = static_cast<VkResult>(vk::Result::eErrorOutOfDeviceMemory);

struct ExpectedArrayView
{
    VkImage image;
    VkImageView view;
    VkImageViewType type;
    std::uint32_t baseLayer;
    std::uint32_t layerCount;
    VkResult result {success};
};

auto description(panda::PixelFormat format = panda::PixelFormat::Rgba8Unorm) -> panda::TextureDesc
{
    return {
        .extent = {32, 16},
        .format = format,
        .usage = panda::TextureUsage::TransferDestination | panda::TextureUsage::Sampled
    };
}

auto unsupportedPixelFormat() -> panda::PixelFormat
{
    return std::bit_cast<panda::PixelFormat>(std::uint8_t {0xff});
}

auto unsupportedTextureUsage() -> panda::TextureUsage
{
    return std::bit_cast<panda::TextureUsage>(std::uint8_t {0x80});
}

auto unsupportedMipPolicy() -> panda::TextureMipPolicy
{
    return std::bit_cast<panda::TextureMipPolicy>(std::uint8_t {0xff});
}

auto unsupportedAddressMode() -> panda::TextureAddressMode
{
    return std::bit_cast<panda::TextureAddressMode>(std::uint8_t {0xff});
}

class TextureResourceTest : public testing::Test
{
protected:
    void SetUp() override
    {
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceMemoryProperties(_, NotNull()))
            .WillOnce([](VkPhysicalDevice, VkPhysicalDeviceMemoryProperties* output) {
                output->memoryTypeCount = 1;
                output->memoryTypes[0] = {
                    .propertyFlags = static_cast<VkMemoryPropertyFlags>(vk::MemoryPropertyFlagBits::eDeviceLocal),
                    .heapIndex = 0};
                output->memoryHeapCount = 1;
                output->memoryHeaps[0] = {.size = 4096, .flags = 0};
            });
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceProperties2(_, NotNull()))
            .WillOnce([](VkPhysicalDevice, VkPhysicalDeviceProperties2* output) {
                output->properties.limits.maxMemoryAllocationCount = 8;
                auto* maintenance = static_cast<VkPhysicalDeviceMaintenance3Properties*>(output->pNext);
                ASSERT_NE(maintenance, nullptr);
                maintenance->maxMemoryAllocationSize = 4096;
            });
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceProperties(_, NotNull()))
            .Times(testing::AnyNumber())
            .WillRepeatedly([](VkPhysicalDevice, VkPhysicalDeviceProperties* output) {
                output->limits.maxImageArrayLayers = 2;
            });
        EXPECT_CALL(
            vulkan,
            vkGetPhysicalDeviceImageFormatProperties(_, _, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, _, 0, NotNull()))
            .Times(testing::AnyNumber())
            .WillRepeatedly([](VkPhysicalDevice,
                               VkFormat,
                               VkImageType,
                               VkImageTiling,
                               VkImageUsageFlags,
                               VkImageCreateFlags,
                               VkImageFormatProperties* output) {
                *output = VkImageFormatProperties {};
                output->maxExtent = {.width = 4096, .height = 4096, .depth = 1};
                output->maxArrayLayers = 2;
                return success;
            });
        EXPECT_CALL(poolMock, destroy(_));
        pool = MemoryPool::create(physical, device);
    }

    void TearDown() override
    {
        pool.reset();
        std::ignore = device.release();
        std::ignore = instance.release();
    }

    void expectImage(VkImage image,
                     const panda::detail::TextureResourceCreateInfo& desc,
                     VkResult result = success,
                     vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1)
    {
        EXPECT_CALL(vulkan, vkCreateImage(deviceHandle, NotNull(), nullptr, NotNull()))
            .WillOnce([image, desc, result, samples](VkDevice,
                                                     const VkImageCreateInfo* info,
                                                     const VkAllocationCallbacks*,
                                                     VkImage* output) {
                EXPECT_EQ(info->imageType, VK_IMAGE_TYPE_2D);
                EXPECT_EQ(info->format, static_cast<VkFormat>(desc.format));
                EXPECT_EQ(info->extent.width, desc.extent.width);
                EXPECT_EQ(info->extent.height, desc.extent.height);
                EXPECT_EQ(info->extent.depth, 1);
                EXPECT_EQ(info->samples, static_cast<VkSampleCountFlagBits>(samples));
                EXPECT_EQ(info->usage, static_cast<VkImageUsageFlags>(desc.usage));
                if (result == success)
                {
                    *output = image;
                }
                return result;
            });
    }

    void expectImage(VkImage image,
                     const panda::TextureDesc& desc,
                     VkResult result = success,
                     vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1)
    {
        const auto nativeDescription = panda::detail::TextureResourceCreateInfo {
            .extent = {.width = desc.extent.x, .height = desc.extent.y},
            .format = panda::expect(panda::detail::mapPixelFormat(desc.format), "Map texture format"),
            .usage = panda::expect(panda::detail::mapTextureUsage(desc.usage), "Map texture usage")
        };
        expectImage(image, nativeDescription, result, samples);
    }

    void expectRequirements(VkImage image)
    {
        EXPECT_CALL(vulkan, vkGetImageMemoryRequirements2(deviceHandle, NotNull(), NotNull()))
            .WillOnce([image](VkDevice, const VkImageMemoryRequirementsInfo2* info, VkMemoryRequirements2* output) {
                EXPECT_EQ(info->image, image);
                output->memoryRequirements = {.size = 128, .alignment = 64, .memoryTypeBits = 1};
                auto* dedicated = static_cast<VkMemoryDedicatedRequirements*>(output->pNext);
                ASSERT_NE(dedicated, nullptr);
                dedicated->requiresDedicatedAllocation = vk::False;
            });
    }

    void expectBind(VkImage image, VkDeviceMemory memory, VkResult result = success)
    {
        EXPECT_CALL(vulkan, vkBindImageMemory2(deviceHandle, 1, NotNull()))
            .WillOnce([image, memory, result](VkDevice, std::uint32_t, const VkBindImageMemoryInfo* info) {
                EXPECT_EQ(info->image, image);
                EXPECT_EQ(info->memory, memory);
                EXPECT_EQ(info->memoryOffset, 32);
                return result;
            });
    }

    void expectView(VkImage image,
                    VkImageView view,
                    VkResult result = success,
                    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT)
    {
        EXPECT_CALL(vulkan, vkCreateImageView(deviceHandle, NotNull(), nullptr, NotNull()))
            .WillOnce([image, view, result, aspect](VkDevice,
                                                    const VkImageViewCreateInfo* info,
                                                    const VkAllocationCallbacks*,
                                                    VkImageView* output) {
                EXPECT_EQ(info->image, image);
                EXPECT_EQ(info->viewType, VK_IMAGE_VIEW_TYPE_2D);
                EXPECT_EQ(info->subresourceRange.aspectMask, aspect);
                if (result == success)
                {
                    *output = view;
                }
                return result;
            });
    }

    void expectArrayView(testing::Sequence& creation, const ExpectedArrayView& expected)
    {
        EXPECT_CALL(vulkan, vkCreateImageView(deviceHandle, NotNull(), nullptr, NotNull()))
            .InSequence(creation)
            .WillOnce([expected](VkDevice,
                                 const VkImageViewCreateInfo* info,
                                 const VkAllocationCallbacks*,
                                 VkImageView* output) {
                EXPECT_EQ(info->image, expected.image);
                EXPECT_EQ(info->viewType, expected.type);
                EXPECT_EQ(info->subresourceRange.baseArrayLayer, expected.baseLayer);
                EXPECT_EQ(info->subresourceRange.layerCount, expected.layerCount);
                if (expected.result == success)
                {
                    *output = expected.view;
                }
                return expected.result;
            });
    }

    void expectBoundImage(VkImage image,
                          const panda::detail::TextureResourceCreateInfo& desc,
                          vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1)
    {
        expectImage(image, desc, success, samples);
        expectRequirements(image);
        EXPECT_CALL(poolMock, allocate(Ref(*pool), _))
            .WillRepeatedly([](MemoryPool&, const panda::core::vulkan::resources::MemoryRequest&) {
                return std::uint64_t {9};
            });
        EXPECT_CALL(allocationMock, getMemory(9)).WillRepeatedly([](std::uint64_t) {
            return vk::DeviceMemory {fakeHandle<VkDeviceMemory>(30)};
        });
        EXPECT_CALL(allocationMock, getRange(9)).WillRepeatedly([](std::uint64_t) {
            return panda::core::vulkan::resources::MemoryRange {.offset = 32, .size = 128};
        });
        expectBind(image, fakeHandle<VkDeviceMemory>(30));
    }

    void expectBoundImage(VkImage image,
                          const panda::TextureDesc& desc = description(),
                          vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1)
    {
        const auto nativeDescription = panda::detail::TextureResourceCreateInfo {
            .extent = {.width = desc.extent.x, .height = desc.extent.y},
            .format = panda::expect(panda::detail::mapPixelFormat(desc.format), "Map texture format"),
            .usage = panda::expect(panda::detail::mapTextureUsage(desc.usage), "Map texture usage")
        };
        expectBoundImage(image, nativeDescription, samples);
    }

    panda::test::ScopedMock<panda::test::VulkanMock> vulkan;
    panda::test::ScopedMock<panda::test::MemoryPoolMock> poolMock;
    panda::test::ScopedMock<panda::test::MemoryAllocationMock> allocationMock;
    vk::raii::Context loader {vkGetInstanceProcAddr};
    vk::raii::Instance instance {loader, fakeHandle<VkInstance>(1)};
    vk::raii::PhysicalDevice physical {instance, fakeHandle<VkPhysicalDevice>(2)};
    VkDevice deviceHandle {fakeHandle<VkDevice>(3)};
    vk::raii::Device device {physical, deviceHandle};
    std::unique_ptr<MemoryPool> pool;
};
}

TEST(ResourceMappings, MapsOnlyPublicTextureFormatsAndUsages)
{
    constexpr auto formats = std::array {
        std::pair {panda::PixelFormat::Rgba8Unorm,  vk::Format::eR8G8B8A8Unorm     },
        std::pair {panda::PixelFormat::Rgba8Srgb,   vk::Format::eR8G8B8A8Srgb      },
        std::pair {panda::PixelFormat::Bgra8Unorm,  vk::Format::eB8G8R8A8Unorm     },
        std::pair {panda::PixelFormat::Bgra8Srgb,   vk::Format::eB8G8R8A8Srgb      },
        std::pair {panda::PixelFormat::Rgba16Float, vk::Format::eR16G16B16A16Sfloat}
    };
    for (const auto& [format, native] : formats)
    {
        EXPECT_EQ(*panda::detail::mapPixelFormat(format), native);
    }
    EXPECT_EQ(panda::detail::mapPixelFormat(panda::PixelFormat::Undefined).error().code,
              panda::ErrorCode::InvalidArgument);
    EXPECT_EQ(panda::detail::mapPixelFormat(unsupportedPixelFormat()).error().code, panda::ErrorCode::Unsupported);

    EXPECT_EQ(*panda::detail::mapTextureUsage(panda::TextureUsage::TransferDestination),
              vk::ImageUsageFlagBits::eTransferDst);
    EXPECT_EQ(*panda::detail::mapTextureUsage(panda::TextureUsage::Sampled), vk::ImageUsageFlagBits::eSampled);
    EXPECT_EQ(*panda::detail::mapTextureUsage(panda::TextureUsage::TransferDestination | panda::TextureUsage::Sampled),
              vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled);
    EXPECT_EQ(*panda::detail::mapTextureUsage(panda::TextureUsage::None), vk::ImageUsageFlags {});
    EXPECT_EQ(panda::detail::mapTextureUsage(unsupportedTextureUsage()).error().code, panda::ErrorCode::Unsupported);
}

TEST(Texture, EmptyHandleReportsZeroMipLevels)
{
    EXPECT_EQ(panda::Texture {}.getMipLevels(), 0U);
}

TEST_F(TextureResourceTest, RejectsInvalidDescriptionsBeforeCreatingNativeObjects)
{
    EXPECT_CALL(vulkan,
                vkGetPhysicalDeviceImageFormatProperties(_, _, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, _, 0, _))
        .Times(0);
    EXPECT_CALL(vulkan, vkCreateImage(_, _, _, _)).Times(0);
    const auto empty = TextureResource::create(
        device,
        *pool,
        {.extent = {}, .format = panda::PixelFormat::Rgba8Unorm, .usage = panda::TextureUsage::Sampled});
    const auto undefined = TextureResource::create(device,
                                                   *pool,
                                                   {
                                                       .extent = {1, 1},
                                                       .format = panda::PixelFormat::Undefined,
                                                       .usage = panda::TextureUsage::Sampled
    });

    ASSERT_FALSE(empty);
    ASSERT_FALSE(undefined);
    EXPECT_EQ(empty.error().code, panda::ErrorCode::InvalidArgument);
    EXPECT_EQ(undefined.error().code, panda::ErrorCode::InvalidArgument);

    const auto transferOnly = TextureResource::create(device,
                                                      *pool,
                                                      {
                                                          .extent = {1, 1},
                                                          .format = panda::PixelFormat::Rgba8Unorm,
                                                          .usage = panda::TextureUsage::TransferDestination
    });
    ASSERT_FALSE(transferOnly);
    EXPECT_EQ(transferOnly.error().code, panda::ErrorCode::InvalidArgument);
}

TEST_F(TextureResourceTest, RejectsUnknownPublicCatalogValuesBeforeCreatingNativeObjects)
{
    EXPECT_CALL(vulkan,
                vkGetPhysicalDeviceImageFormatProperties(_, _, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, _, 0, _))
        .Times(0);
    EXPECT_CALL(vulkan, vkCreateImage(_, _, _, _)).Times(0);
    const auto unknownFormat =
        TextureResource::create(device,
                                *pool,
                                {
                                    .extent = {1, 1},
                                    .format = unsupportedPixelFormat(),
                                    .usage = panda::TextureUsage::TransferDestination | panda::TextureUsage::Sampled
    });
    const auto unknownUsage = TextureResource::create(device,
                                                      *pool,
                                                      {
                                                          .extent = {1, 1},
                                                          .format = panda::PixelFormat::Rgba8Unorm,
                                                          .usage = unsupportedTextureUsage()
    });

    ASSERT_FALSE(unknownFormat);
    ASSERT_FALSE(unknownUsage);
    EXPECT_EQ(unknownFormat.error().code, panda::ErrorCode::Unsupported);
    EXPECT_EQ(unknownUsage.error().code, panda::ErrorCode::Unsupported);
}

TEST_F(TextureResourceTest, RejectsInvalidMipPolicyAndAddressModeBeforeCreatingNativeObjects)
{
    EXPECT_CALL(vulkan,
                vkGetPhysicalDeviceImageFormatProperties(_, _, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, _, 0, _))
        .Times(0);
    EXPECT_CALL(vulkan, vkCreateImage(_, _, _, _)).Times(0);
    auto invalidPolicy = description();
    invalidPolicy.mipPolicy = unsupportedMipPolicy();
    auto invalidAddressMode = description();
    invalidAddressMode.addressMode = unsupportedAddressMode();

    const auto policy = TextureResource::create(device, *pool, invalidPolicy);
    const auto addressMode = TextureResource::create(device, *pool, invalidAddressMode);

    ASSERT_FALSE(policy);
    EXPECT_EQ(policy.error().code, panda::ErrorCode::InvalidArgument);
    ASSERT_FALSE(addressMode);
    EXPECT_EQ(addressMode.error().code, panda::ErrorCode::InvalidArgument);
}

TEST_F(TextureResourceTest, RejectsUnsupportedFormatUsageAndExtentBeforeCreatingImage)
{
    const auto desc = description();
    EXPECT_CALL(
        vulkan,
        vkGetPhysicalDeviceImageFormatProperties(_, _, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, _, 0, NotNull()))
        .WillOnce([](VkPhysicalDevice,
                     VkFormat,
                     VkImageType,
                     VkImageTiling,
                     VkImageUsageFlags,
                     VkImageCreateFlags,
                     VkImageFormatProperties*) {
            return static_cast<VkResult>(vk::Result::eErrorFormatNotSupported);
        });
    EXPECT_CALL(vulkan, vkCreateImage(_, _, _, _)).Times(0);
    const auto unsupported = TextureResource::create(device, *pool, desc);
    ASSERT_FALSE(unsupported);
    EXPECT_EQ(unsupported.error().code, panda::ErrorCode::Unsupported);
}

TEST_F(TextureResourceTest, RejectsExtentBeyondDeviceLimitBeforeCreatingImage)
{
    EXPECT_CALL(
        vulkan,
        vkGetPhysicalDeviceImageFormatProperties(_, _, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, _, 0, NotNull()))
        .WillOnce([](VkPhysicalDevice,
                     VkFormat,
                     VkImageType,
                     VkImageTiling,
                     VkImageUsageFlags,
                     VkImageCreateFlags,
                     VkImageFormatProperties* output) {
            *output = VkImageFormatProperties {};
            output->maxExtent = {.width = 31, .height = 16, .depth = 1};
            return success;
        });
    EXPECT_CALL(vulkan, vkCreateImage(_, _, _, _)).Times(0);
    auto desc = description();
    desc.extent.x = 32;
    const auto oversized = TextureResource::create(device, *pool, desc);
    ASSERT_FALSE(oversized);
    EXPECT_EQ(oversized.error().code, panda::ErrorCode::InvalidArgument);
}

TEST_F(TextureResourceTest, RejectsCubemapWhenDeviceCannotSupportSixArrayLayers)
{
    auto desc = description(panda::PixelFormat::Rgba16Float);
    desc.extent = {16, 16};
    desc.type = panda::TextureType::CubeMap;
    EXPECT_CALL(vulkan,
                vkGetPhysicalDeviceImageFormatProperties(_,
                                                         _,
                                                         VK_IMAGE_TYPE_2D,
                                                         VK_IMAGE_TILING_OPTIMAL,
                                                         _,
                                                         VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
                                                         NotNull()))
        .WillOnce([](VkPhysicalDevice,
                     VkFormat,
                     VkImageType,
                     VkImageTiling,
                     VkImageUsageFlags,
                     VkImageCreateFlags,
                     VkImageFormatProperties* output) {
            *output = VkImageFormatProperties {};
            output->maxExtent = {.width = 4096, .height = 4096, .depth = 1};
            output->maxMipLevels = 16;
            output->maxArrayLayers = 2;
            return success;
        });
    EXPECT_CALL(vulkan, vkCreateImage(_, _, _, _)).Times(0);

    const auto result = TextureResource::create(device, *pool, desc);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, panda::ErrorCode::Unsupported);
}

TEST_F(TextureResourceTest, RejectsInvalidSamplesAndMultisampledNonAttachments)
{
    EXPECT_CALL(vulkan,
                vkGetPhysicalDeviceImageFormatProperties(_, _, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, _, 0, _))
        .Times(0);
    EXPECT_CALL(vulkan, vkCreateImage(_, _, _, _)).Times(0);
    // Deliberately invalid sample count to verify rejection before Vulkan calls.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    const auto invalidSampleCount = static_cast<vk::SampleCountFlagBits>(3);
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    const auto zeroSampleCount = static_cast<vk::SampleCountFlagBits>(0);
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    const auto excessiveSampleCount = static_cast<vk::SampleCountFlagBits>(128);
    const auto invalidCount = TextureResource::create(device, *pool, description(), invalidSampleCount);
    const auto zeroCount = TextureResource::create(device, *pool, description(), zeroSampleCount);
    const auto excessiveCount = TextureResource::create(device, *pool, description(), excessiveSampleCount);
    const auto sampledTexture = TextureResource::create(device, *pool, description(), vk::SampleCountFlagBits::e4);

    ASSERT_FALSE(invalidCount);
    EXPECT_EQ(invalidCount.error().code, panda::ErrorCode::InvalidArgument);
    ASSERT_FALSE(zeroCount);
    EXPECT_EQ(zeroCount.error().code, panda::ErrorCode::InvalidArgument);
    ASSERT_FALSE(excessiveCount);
    EXPECT_EQ(excessiveCount.error().code, panda::ErrorCode::InvalidArgument);
    ASSERT_FALSE(sampledTexture);
    EXPECT_EQ(sampledTexture.error().code, panda::ErrorCode::Unsupported);
}

TEST_F(TextureResourceTest, RejectsUploadReservationForMultisampledAttachment)
{
    const auto desc = panda::detail::TextureResourceCreateInfo {
        .extent = {.width = 32, .height = 16},
        .format = vk::Format::eR8G8B8A8Unorm,
        .usage = vk::ImageUsageFlagBits::eColorAttachment
    };
    auto* const image = fakeHandle<VkImage>(20);
    auto* const view = fakeHandle<VkImageView>(21);
    expectBoundImage(image, desc, vk::SampleCountFlagBits::e4);
    expectView(image, view, success, VK_IMAGE_ASPECT_COLOR_BIT);
    auto cleanup = testing::Sequence {};
    EXPECT_CALL(vulkan, vkDestroyImageView(deviceHandle, view, nullptr)).InSequence(cleanup);
    EXPECT_CALL(vulkan, vkDestroyImage(deviceHandle, image, nullptr)).InSequence(cleanup);
    EXPECT_CALL(poolMock, release(Ref(*pool), 9)).InSequence(cleanup);

    {
        auto created = TextureResource::createNative(device, *pool, desc, vk::SampleCountFlagBits::e4);
        ASSERT_TRUE(created);
        const auto reservation = (*created)->reserveUpload(panda::core::vulkan::upload::UploadBatchId {1});
        ASSERT_FALSE(reservation);
        EXPECT_EQ(reservation.error().code, panda::ErrorCode::Unsupported);
    }
}

TEST_F(TextureResourceTest, PreservesImageCreationFailureWithoutDestroyingItsUndefinedOutput)
{
    expectImage(fakeHandle<VkImage>(20), description(), exhausted);

    const auto created = TextureResource::create(device, *pool, description());

    ASSERT_FALSE(created);
    EXPECT_EQ(created.error().code, panda::ErrorCode::ResourceExhausted);
}

TEST_F(TextureResourceTest, DestroysAnUnboundImageWhenPoolAllocationFails)
{
    auto* const image = fakeHandle<VkImage>(20);
    expectImage(image, description());
    expectRequirements(image);
    EXPECT_CALL(poolMock, allocate(Ref(*pool), _))
        .WillOnce(Return(panda::Result<std::uint64_t> {std::unexpected {
            panda::makeError(panda::ErrorCode::ResourceExhausted, "Controlled image allocation failure")}}));
    EXPECT_CALL(vulkan, vkDestroyImage(deviceHandle, image, nullptr));

    const auto created = TextureResource::create(device, *pool, description());

    ASSERT_FALSE(created);
    EXPECT_EQ(created.error().code, panda::ErrorCode::ResourceExhausted);
}

TEST_F(TextureResourceTest, DestroysBoundImageBeforeReleasingAllocationWhenViewCreationFails)
{
    auto* const image = fakeHandle<VkImage>(20);
    auto* const view = fakeHandle<VkImageView>(21);
    expectBoundImage(image);
    expectView(image, view, exhausted);
    auto cleanup = testing::Sequence {};
    EXPECT_CALL(vulkan, vkDestroyImageView(deviceHandle, view, nullptr)).Times(0);
    EXPECT_CALL(vulkan, vkDestroyImage(deviceHandle, image, nullptr)).InSequence(cleanup);
    EXPECT_CALL(poolMock, release(Ref(*pool), 9)).InSequence(cleanup);

    const auto created = TextureResource::create(device, *pool, description());

    ASSERT_FALSE(created);
    EXPECT_EQ(created.error().code, panda::ErrorCode::ResourceExhausted);
}

TEST_F(TextureResourceTest, OwnsItsViewImageAndAllocationAndTracksBatchReservations)
{
    auto* const image = fakeHandle<VkImage>(20);
    auto* const view = fakeHandle<VkImageView>(21);
    expectBoundImage(image);
    expectView(image, view);
    auto cleanup = testing::Sequence {};
    EXPECT_CALL(vulkan, vkDestroyImageView(deviceHandle, view, nullptr)).InSequence(cleanup);
    EXPECT_CALL(vulkan, vkDestroyImage(deviceHandle, image, nullptr)).InSequence(cleanup);
    EXPECT_CALL(poolMock, release(Ref(*pool), 9)).InSequence(cleanup);

    {
        auto created = TextureResource::create(device, *pool, description());
        ASSERT_TRUE(created);
        EXPECT_EQ((*created)->getExtent(), (vk::Extent2D {.width = 32, .height = 16}));
        EXPECT_EQ((*created)->getFormat(), vk::Format::eR8G8B8A8Unorm);
        EXPECT_EQ((*created)->getUsage(), vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled);
        EXPECT_EQ((*created)->getPublicFormat(), panda::PixelFormat::Rgba8Unorm);
        EXPECT_EQ((*created)->getPublicUsage(),
                  panda::TextureUsage::TransferDestination | panda::TextureUsage::Sampled);
        EXPECT_EQ((*created)->getNative(), vk::Image {image});

        const auto firstBatch = panda::core::vulkan::upload::UploadBatchId {1};
        const auto otherBatch = panda::core::vulkan::upload::UploadBatchId {2};
        ASSERT_TRUE((*created)->reserveUpload(firstBatch));
        EXPECT_FALSE((*created)->reserveUpload(otherBatch));
        (*created)->cancelUpload(firstBatch);
        ASSERT_TRUE((*created)->reserveUpload(otherBatch));
        (*created)->commitUpload(otherBatch);
        EXPECT_FALSE((*created)->reserveUpload(firstBatch));
    }
}

TEST_F(TextureResourceTest, CreatesArrayAndPerLayerViewsForArrayImages)
{
    auto* const image = fakeHandle<VkImage>(20);
    auto* const imageView = fakeHandle<VkImageView>(21);
    auto* const arrayView = fakeHandle<VkImageView>(22);
    auto* const layerView = fakeHandle<VkImageView>(23);
    const auto desc = panda::detail::TextureResourceCreateInfo {
        .extent = {.width = 32, .height = 16},
        .format = vk::Format::eR8G8B8A8Unorm,
        .usage = vk::ImageUsageFlagBits::eColorAttachment,
        .arrayLayers = 2
    };
    expectBoundImage(image, desc);
    auto creation = testing::Sequence {};
    expectArrayView(
        creation,
        {.image = image, .view = imageView, .type = VK_IMAGE_VIEW_TYPE_2D, .baseLayer = 0, .layerCount = 1});
    expectArrayView(
        creation,
        {.image = image, .view = arrayView, .type = VK_IMAGE_VIEW_TYPE_2D_ARRAY, .baseLayer = 0, .layerCount = 2});
    expectArrayView(
        creation,
        {.image = image, .view = layerView, .type = VK_IMAGE_VIEW_TYPE_2D, .baseLayer = 1, .layerCount = 1});
    auto cleanup = testing::Sequence {};
    EXPECT_CALL(vulkan, vkDestroyImageView(deviceHandle, layerView, nullptr)).InSequence(cleanup);
    EXPECT_CALL(vulkan, vkDestroyImageView(deviceHandle, arrayView, nullptr)).InSequence(cleanup);
    EXPECT_CALL(vulkan, vkDestroyImageView(deviceHandle, imageView, nullptr)).InSequence(cleanup);
    EXPECT_CALL(vulkan, vkDestroyImage(deviceHandle, image, nullptr)).InSequence(cleanup);
    EXPECT_CALL(poolMock, release(Ref(*pool), 9)).InSequence(cleanup);

    {
        auto created = TextureResource::createNative(device, *pool, desc);
        ASSERT_TRUE(created);
        EXPECT_EQ((*created)->getNativeView(), vk::ImageView {imageView});
        EXPECT_EQ((*created)->getNativeArrayView(), vk::ImageView {arrayView});
        EXPECT_EQ((*created)->getLayerView(1), vk::ImageView {layerView});
    }
}

TEST_F(TextureResourceTest, CleansUpArrayViewsWhenLayerViewCreationFails)
{
    auto* const image = fakeHandle<VkImage>(20);
    auto* const imageView = fakeHandle<VkImageView>(21);
    auto* const arrayView = fakeHandle<VkImageView>(22);
    const auto desc = panda::detail::TextureResourceCreateInfo {
        .extent = {.width = 32, .height = 16},
        .format = vk::Format::eR8G8B8A8Unorm,
        .usage = vk::ImageUsageFlagBits::eColorAttachment,
        .arrayLayers = 2
    };
    expectBoundImage(image, desc);
    auto creation = testing::Sequence {};
    expectArrayView(
        creation,
        {.image = image, .view = imageView, .type = VK_IMAGE_VIEW_TYPE_2D, .baseLayer = 0, .layerCount = 1});
    expectArrayView(
        creation,
        {.image = image, .view = arrayView, .type = VK_IMAGE_VIEW_TYPE_2D_ARRAY, .baseLayer = 0, .layerCount = 2});
    expectArrayView(creation,
                    {.image = image,
                     .view = nullptr,
                     .type = VK_IMAGE_VIEW_TYPE_2D,
                     .baseLayer = 1,
                     .layerCount = 1,
                     .result = exhausted});
    auto cleanup = testing::Sequence {};
    EXPECT_CALL(vulkan, vkDestroyImageView(deviceHandle, arrayView, nullptr)).InSequence(cleanup);
    EXPECT_CALL(vulkan, vkDestroyImageView(deviceHandle, imageView, nullptr)).InSequence(cleanup);
    EXPECT_CALL(vulkan, vkDestroyImage(deviceHandle, image, nullptr)).InSequence(cleanup);
    EXPECT_CALL(poolMock, release(Ref(*pool), 9)).InSequence(cleanup);

    const auto created = TextureResource::createNative(device, *pool, desc);

    ASSERT_FALSE(created);
    EXPECT_EQ(created.error().code, panda::ErrorCode::ResourceExhausted);
}
