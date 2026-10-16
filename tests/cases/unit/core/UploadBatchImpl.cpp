// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
// clang-format on

#include "upload/UploadBatchImpl.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <vulkan/vulkan_core.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <panda/Buffer.hpp>
#include <panda/CompletionPoint.hpp>
#include <panda/Error.hpp>
#include <panda/Mesh.hpp>
#include <panda/Texture.hpp>
#include <panda/UploadBatch.hpp>
#include <span>
#include <utility>

#include "ScopedMock.hpp"
#include "SubmissionQueue.hpp"
#include "VulkanTestSupport.hpp"
#include "external/vulkan/VulkanMock.hpp"
#include "panda/core/BufferResourceMock.hpp"
#include "panda/core/MemoryAllocationMock.hpp"
#include "panda/core/MemoryPoolMock.hpp"
#include "panda/core/MeshLayoutMock.hpp"
#include "panda/core/MeshResourceMock.hpp"
#include "panda/core/SubmissionQueueMock.hpp"
#include "panda/core/TextureResourceMock.hpp"
#include "panda/core/UploadManagerMock.hpp"
#include "resources/BufferResource.hpp"
#include "resources/MeshLayout.hpp"
#include "resources/MeshResource.hpp"
#include "resources/TextureResource.hpp"
#include "resources/memory/MemoryPool.hpp"
#include "upload/UploadBatchId.hpp"
#include "upload/UploadManager.hpp"
#include "upload/UploadOperation.hpp"

namespace
{
using panda::core::vulkan::resources::MemoryPool;
using panda::core::vulkan::resources::MeshLayout;
using panda::core::vulkan::upload::BufferUpload;
using panda::core::vulkan::upload::TextureUpload;
using panda::core::vulkan::upload::UploadBatchId;
using panda::core::vulkan::upload::UploadManager;
using panda::core::vulkan::upload::UploadOperation;
using panda::detail::BufferResource;
using panda::detail::MeshResource;
using panda::detail::TextureResource;
using panda::detail::UploadBatchImpl;
using panda::test::fakeVulkanHandle;
using testing::_;
using testing::NotNull;
using testing::Ref;
using testing::Return;
using testing::ReturnRef;

auto bufferDescription(std::uint64_t size) -> panda::BufferDesc
{
    return {.size = size,
            .usage = vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst,
            .memory = panda::BufferMemory::Upload};
}

auto textureDescription() -> panda::TextureDesc
{
    return {
        .extent = vk::Extent2D {.width = 16, .height = 16},
        .format = vk::Format::eR8G8B8A8Unorm,
        .usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled
    };
}

class UploadBatchImplTest : public testing::Test
{
protected:
    void SetUp() override
    {
        EXPECT_CALL(submission, construct(Ref(device), 0));
        submissions = std::make_unique<panda::detail::SubmissionQueue>(device, 0);

        EXPECT_CALL(vulkan, vkGetPhysicalDeviceMemoryProperties(_, NotNull()))
            .WillOnce([](VkPhysicalDevice, VkPhysicalDeviceMemoryProperties* properties) {
                properties->memoryTypeCount = 1;
                properties->memoryTypes[0] = {
                    .propertyFlags = static_cast<VkMemoryPropertyFlags>(vk::MemoryPropertyFlagBits::eDeviceLocal),
                    .heapIndex = 0};
                properties->memoryHeapCount = 1;
                properties->memoryHeaps[0] = {.size = 4096, .flags = 0};
            });
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceProperties2(_, NotNull()))
            .WillOnce([](VkPhysicalDevice, VkPhysicalDeviceProperties2* properties) {
                properties->properties.limits.maxMemoryAllocationCount = 8;
                auto* maintenance = static_cast<VkPhysicalDeviceMaintenance3Properties*>(properties->pNext);
                ASSERT_NE(maintenance, nullptr);
                maintenance->maxMemoryAllocationSize = 4096;
            });
        EXPECT_CALL(poolMock, destroy(_));
        pool = MemoryPool::create(physical, device);
        EXPECT_CALL(managerMock, destroy(_));
        manager = std::make_unique<UploadManager>(device, *pool, *submissions, 0);

        ON_CALL(buffers, getSize(_)).WillByDefault(Return(4096));
    }

    void TearDown() override
    {
        manager.reset();
        submissions.reset();
        pool.reset();
        std::ignore = device.release();
        std::ignore = instance.release();
    }

    auto makeBatch() -> std::unique_ptr<UploadBatchImpl>
    {
        EXPECT_CALL(managerMock, retainBatch(Ref(*manager)));
        EXPECT_CALL(managerMock, releaseBatch(Ref(*manager)));
        return std::make_unique<UploadBatchImpl>(*manager, UploadBatchId {17});
    }

    auto makeBuffer(std::uint64_t size) -> std::shared_ptr<BufferResource>
    {
        const auto description = bufferDescription(size);
        // GMock's WillOnce wrapper triggers a false-positive leak report in clang-analyzer.
        EXPECT_CALL(buffers, create(Ref(device), Ref(*pool), testing::Field(&panda::BufferDesc::size, size)))
            .Times(1)
            .WillRepeatedly(Return(panda::Result<void> {}));
        auto created = BufferResource::create(device, *pool, description);
        EXPECT_TRUE(created);
        return created ? std::move(*created) : nullptr;
    }

    auto makeTexture() -> std::shared_ptr<TextureResource>
    {
        const auto description = textureDescription();
        // GMock's WillOnce wrapper triggers a false-positive leak report in clang-analyzer.
        EXPECT_CALL(textures, create(Ref(device), Ref(*pool), _))
            .Times(1)
            .WillRepeatedly(Return(panda::Result<void> {}));
        auto created = TextureResource::create(device, *pool, description);
        EXPECT_TRUE(created);
        return created ? std::move(*created) : nullptr;
    }

    auto makeMesh() -> std::shared_ptr<MeshResource>
    {
        const auto description = panda::MeshDesc {
            .vertexCount = 3,
            .vertexStride = 4,
            .indices = panda::MeshIndexDesc {.count = 3, .type = panda::MeshIndexType::UInt16}
        };
        meshVertexBuffer = makeBuffer(12);
        meshIndexBuffer = makeBuffer(6);
        EXPECT_CALL(layouts, create(_))
            .WillOnce(
                Return(panda::test::MeshLayoutPlan {.description = description, .vertexSize = 12, .indexSize = 6}));
        auto layout = MeshLayout::create(description);
        EXPECT_TRUE(layout);
        if (!layout)
        {
            return nullptr;
        }
        meshLayout = std::make_unique<MeshLayout>(*layout);
        EXPECT_CALL(meshes, create(Ref(device), Ref(*pool), _))
            .WillOnce(Return(panda::Result<panda::test::MeshResourcePlan> {
                panda::test::MeshResourcePlan {.vertexBuffer = meshVertexBuffer,
                                               .indexBuffer = meshIndexBuffer,
                                               .layout = meshLayout.get()}
        }));
        ON_CALL(meshes, getVertexBuffer(_)).WillByDefault(ReturnRef(meshVertexBuffer));
        ON_CALL(meshes, getIndexBuffer(_)).WillByDefault(ReturnRef(meshIndexBuffer));
        ON_CALL(meshes, getLayout(_)).WillByDefault(ReturnRef(*meshLayout));
        auto created = MeshResource::create(device, *pool, description);
        EXPECT_TRUE(created);
        return created ? std::move(*created) : nullptr;
    }

    panda::test::ScopedMock<panda::test::VulkanMock> vulkan;
    panda::test::ScopedMock<panda::test::MemoryAllocationMock> allocations;
    panda::test::ScopedMock<panda::test::MemoryPoolMock> poolMock;
    panda::test::ScopedMock<panda::test::SubmissionQueueMock> submission;
    panda::test::ScopedMock<panda::test::UploadManagerMock> managerMock;
    panda::test::ScopedMock<panda::test::BufferResourceMock> buffers;
    panda::test::ScopedMock<panda::test::TextureResourceMock> textures;
    panda::test::ScopedMock<panda::test::MeshResourceMock> meshes;
    panda::test::ScopedMock<panda::test::MeshLayoutMock> layouts;
    vk::raii::Context loader {vkGetInstanceProcAddr};
    vk::raii::Instance instance {loader, fakeVulkanHandle<VkInstance>(1)};
    vk::raii::PhysicalDevice physical {instance, fakeVulkanHandle<VkPhysicalDevice>(2)};
    vk::raii::Device device {physical, fakeVulkanHandle<VkDevice>(3)};
    std::unique_ptr<MemoryPool> pool;
    std::unique_ptr<panda::detail::SubmissionQueue> submissions;
    std::unique_ptr<UploadManager> manager;
    std::unique_ptr<MeshLayout> meshLayout;
    std::shared_ptr<BufferResource> meshVertexBuffer;
    std::shared_ptr<BufferResource> meshIndexBuffer;
};
}

TEST_F(UploadBatchImplTest, ReturnsBufferValidationFailureWithoutCollectingOrAllocating)
{
    auto destination = makeBuffer(64);
    auto batch = makeBatch();
    const auto payload = std::array {std::byte {1}, std::byte {2}};
    const auto failure = panda::Result<void> {
        std::unexpected {panda::makeError(panda::ErrorCode::InvalidArgument, "Controlled validation failure")}};
    EXPECT_CALL(managerMock, validateWrite(Ref(*manager), destination, 8, payload.size())).WillOnce(Return(failure));

    const auto result = batch->write(destination, 8, payload);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, panda::ErrorCode::InvalidArgument);
}

TEST_F(UploadBatchImplTest, ReturnsCollectionFailureBeforeCreatingAStagingPage)
{
    auto destination = makeBuffer(64);
    auto batch = makeBatch();
    const auto payload = std::array {std::byte {1}, std::byte {2}};
    EXPECT_CALL(managerMock, validateWrite(Ref(*manager), destination, 8, payload.size()))
        .WillOnce(Return(panda::Result<void> {}));
    EXPECT_CALL(managerMock, collect(Ref(*manager)))
        .WillOnce(Return(panda::Result<void> {
            std::unexpected {panda::makeError(panda::ErrorCode::DeviceLost, "Controlled collection failure")}}));

    const auto result = batch->write(destination, 8, payload);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, panda::ErrorCode::DeviceLost);
}

TEST_F(UploadBatchImplTest, ReusesStagingAndKeepsOrderedBufferWritesForRetry)
{
    auto destination = makeBuffer(64);
    auto page = makeBuffer(4096);
    auto batch = makeBatch();
    const auto first = std::array {std::byte {1}, std::byte {2}, std::byte {3}};
    const auto second = std::array {std::byte {4}, std::byte {5}};

    EXPECT_CALL(managerMock, validateWrite(Ref(*manager), destination, 12, first.size()))
        .WillOnce(Return(panda::Result<void> {}));
    EXPECT_CALL(managerMock, validateWrite(Ref(*manager), destination, 24, second.size()))
        .WillOnce(Return(panda::Result<void> {}));
    EXPECT_CALL(managerMock, collect(Ref(*manager))).Times(2).WillRepeatedly(Return(panda::Result<void> {}));
    EXPECT_CALL(managerMock, createStaging(Ref(*manager), 4096)).WillOnce(Return(page));
    EXPECT_CALL(buffers, writeMapped(Ref(*page), 0, _))
        .WillOnce([&](const BufferResource&, std::uint64_t, std::span<const std::byte> bytes) {
            EXPECT_THAT(bytes, testing::ElementsAreArray(first));
            return panda::Result<void> {};
        });
    EXPECT_CALL(buffers, writeMapped(Ref(*page), 3, _))
        .WillOnce([&](const BufferResource&, std::uint64_t, std::span<const std::byte> bytes) {
            EXPECT_THAT(bytes, testing::ElementsAreArray(second));
            return panda::Result<void> {};
        });
    EXPECT_CALL(buffers, getSize(Ref(*page))).WillOnce(Return(4096));
    ASSERT_TRUE(batch->write(destination, 12, first));
    ASSERT_TRUE(batch->write(destination, 24, second));

    const auto failure = panda::Result<panda::CompletionPoint> {
        std::unexpected {panda::makeError(panda::ErrorCode::ResourceExhausted, "Controlled submit failure")}};
    EXPECT_CALL(managerMock, submit(Ref(*manager), _, _))
        .WillOnce([&](UploadManager&,
                      std::span<const UploadOperation> writes,
                      std::span<const std::shared_ptr<BufferResource>> pages) {
            EXPECT_EQ(writes.size(), 2);
            EXPECT_EQ(pages.size(), 1);
            if (writes.size() != 2 || pages.size() != 1)
            {
                return failure;
            }
            const auto& firstWrite = std::get<BufferUpload>(writes[0]);
            const auto& secondWrite = std::get<BufferUpload>(writes[1]);
            EXPECT_EQ(firstWrite.staging, page);
            EXPECT_EQ(firstWrite.stagingOffset, 0);
            EXPECT_EQ(firstWrite.destination, destination);
            EXPECT_EQ(firstWrite.offset, 12);
            EXPECT_EQ(firstWrite.size, first.size());
            EXPECT_EQ(secondWrite.staging, page);
            EXPECT_EQ(secondWrite.stagingOffset, 3);
            EXPECT_EQ(secondWrite.destination, destination);
            EXPECT_EQ(secondWrite.offset, 24);
            EXPECT_EQ(secondWrite.size, second.size());
            return failure;
        })
        .WillOnce([&](UploadManager&,
                      std::span<const UploadOperation> writes,
                      std::span<const std::shared_ptr<BufferResource>> pages) {
            EXPECT_EQ(writes.size(), 2);
            EXPECT_EQ(pages.size(), 1);
            return panda::Result<panda::CompletionPoint> {panda::CompletionPoint {}};
        });

    const auto failed = batch->submit();
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code, panda::ErrorCode::ResourceExhausted);

    const auto retried = batch->submit();
    EXPECT_TRUE(retried);
}

TEST_F(UploadBatchImplTest, StagesVertexAndIndexWritesAsOneOrderedMeshOperation)
{
    auto mesh = makeMesh();
    ASSERT_TRUE(mesh);
    ASSERT_NE(meshLayout, nullptr);
    auto page = makeBuffer(4096);
    auto batch = makeBatch();
    const auto vertices = std::array {std::byte {1},
                                      std::byte {2},
                                      std::byte {3},
                                      std::byte {4},
                                      std::byte {5},
                                      std::byte {6},
                                      std::byte {7},
                                      std::byte {8},
                                      std::byte {9},
                                      std::byte {10},
                                      std::byte {11},
                                      std::byte {12}};
    const auto indices =
        std::array {std::byte {13}, std::byte {14}, std::byte {15}, std::byte {16}, std::byte {17}, std::byte {18}};
    EXPECT_CALL(layouts, getVertexSize(_)).WillOnce(Return(12));
    EXPECT_CALL(layouts, getIndexSize(_)).WillOnce(Return(6));
    EXPECT_CALL(meshes, getVertexBuffer(_)).Times(2).WillRepeatedly(ReturnRef(meshVertexBuffer));
    EXPECT_CALL(meshes, getIndexBuffer(_)).Times(3).WillRepeatedly(ReturnRef(meshIndexBuffer));
    EXPECT_CALL(meshes, getLayout(_)).WillOnce(ReturnRef(*meshLayout));
    EXPECT_CALL(managerMock, validateWrite(Ref(*manager), meshVertexBuffer, 0, vertices.size()))
        .WillOnce(Return(panda::Result<void> {}));
    EXPECT_CALL(managerMock, validateWrite(Ref(*manager), meshIndexBuffer, 0, indices.size()))
        .WillOnce(Return(panda::Result<void> {}));
    EXPECT_CALL(managerMock, collect(Ref(*manager))).Times(2).WillRepeatedly(Return(panda::Result<void> {}));
    EXPECT_CALL(managerMock, createStaging(Ref(*manager), 4096)).WillOnce(Return(page));
    EXPECT_CALL(buffers, writeMapped(Ref(*page), 0, _)).WillOnce(Return(panda::Result<void> {}));
    EXPECT_CALL(buffers, writeMapped(Ref(*page), vertices.size(), _)).WillOnce(Return(panda::Result<void> {}));
    EXPECT_CALL(buffers, getSize(Ref(*page))).WillOnce(Return(4096));

    ASSERT_TRUE(batch->write(mesh, vertices, indices));
    EXPECT_CALL(managerMock, submit(Ref(*manager), _, _))
        .WillOnce([&](UploadManager&,
                      std::span<const UploadOperation> writes,
                      std::span<const std::shared_ptr<BufferResource>> pages) {
            EXPECT_EQ(writes.size(), 2);
            EXPECT_EQ(pages.size(), 1);
            if (writes.size() != 2 || pages.size() != 1)
            {
                return panda::Result<panda::CompletionPoint> {panda::CompletionPoint {}};
            }
            const auto& vertexWrite = std::get<BufferUpload>(writes[0]);
            const auto& indexWrite = std::get<BufferUpload>(writes[1]);
            EXPECT_EQ(vertexWrite.destination, meshVertexBuffer);
            EXPECT_EQ(vertexWrite.stagingOffset, 0);
            EXPECT_EQ(vertexWrite.size, vertices.size());
            EXPECT_EQ(indexWrite.destination, meshIndexBuffer);
            EXPECT_EQ(indexWrite.stagingOffset, vertices.size());
            EXPECT_EQ(indexWrite.size, indices.size());
            EXPECT_EQ(vertexWrite.staging, page);
            EXPECT_EQ(indexWrite.staging, page);
            return panda::Result<panda::CompletionPoint> {panda::CompletionPoint {}};
        });

    EXPECT_TRUE(batch->submit());
}

TEST_F(UploadBatchImplTest, AlignsTextureRegionsAndCommitsOneReservationAfterSubmit)
{
    auto texture = makeTexture();
    auto page = makeBuffer(4096);
    auto batch = makeBatch();
    const auto firstBytes = std::array {std::byte {1}, std::byte {2}, std::byte {3}};
    const auto secondBytes = std::array {std::byte {4}, std::byte {5}};
    const auto firstRegion = panda::TextureUploadRegion {
        .offset = {.x = 0,     .y = 0     },
        .extent = {.width = 1, .height = 1}
    };
    const auto secondRegion = panda::TextureUploadRegion {
        .offset = {.x = 1,     .y = 0     },
        .extent = {.width = 1, .height = 1}
    };
    const auto layout = panda::core::vulkan::upload::TextureUploadLayout {.rowLength = 4, .bufferOffsetAlignment = 4};

    EXPECT_CALL(managerMock, validateTextureWrite(Ref(*manager), texture, _, firstBytes.size()))
        .WillOnce(Return(layout));
    EXPECT_CALL(managerMock, validateTextureWrite(Ref(*manager), texture, _, secondBytes.size()))
        .WillOnce(Return(layout));
    EXPECT_CALL(textures, reserveUpload(Ref(*texture), _)).WillOnce(Return(panda::Result<void> {}));
    EXPECT_CALL(managerMock, collect(Ref(*manager))).Times(2).WillRepeatedly(Return(panda::Result<void> {}));
    EXPECT_CALL(managerMock, createStaging(Ref(*manager), 4096)).WillOnce(Return(page));
    EXPECT_CALL(buffers, writeMapped(Ref(*page), 0, _)).WillOnce(Return(panda::Result<void> {}));
    EXPECT_CALL(buffers, writeMapped(Ref(*page), 4, _)).WillOnce(Return(panda::Result<void> {}));
    EXPECT_CALL(buffers, getSize(Ref(*page))).WillOnce(Return(4096));
    ASSERT_TRUE(batch->write(texture, firstBytes, firstRegion));
    ASSERT_TRUE(batch->write(texture, secondBytes, secondRegion));
    EXPECT_CALL(managerMock, submit(Ref(*manager), _, _))
        .WillOnce([&](UploadManager&,
                      std::span<const UploadOperation> writes,
                      std::span<const std::shared_ptr<BufferResource>> pages) {
            EXPECT_EQ(writes.size(), 2);
            EXPECT_EQ(pages.size(), 1);
            if (writes.size() != 2 || pages.size() != 1)
            {
                return panda::Result<panda::CompletionPoint> {panda::CompletionPoint {}};
            }
            const auto& firstWrite = std::get<TextureUpload>(writes[0]);
            const auto& secondWrite = std::get<TextureUpload>(writes[1]);
            EXPECT_TRUE(firstWrite.firstForTexture);
            EXPECT_FALSE(secondWrite.firstForTexture);
            EXPECT_EQ(firstWrite.stagingOffset, 0);
            EXPECT_EQ(secondWrite.stagingOffset, 4);
            EXPECT_EQ(firstWrite.bufferRowLength, 4);
            EXPECT_EQ(secondWrite.bufferRowLength, 4);
            return panda::Result<panda::CompletionPoint> {panda::CompletionPoint {}};
        });
    EXPECT_CALL(textures, commitUpload(Ref(*texture), _));

    EXPECT_TRUE(batch->submit());
}

TEST_F(UploadBatchImplTest, CancelsTextureReservationWhenStagingFails)
{
    auto texture = makeTexture();
    auto batch = makeBatch();
    const auto bytes = std::array {std::byte {1}, std::byte {2}};
    const auto region = panda::TextureUploadRegion {
        .offset = {.x = 0,     .y = 0     },
        .extent = {.width = 1, .height = 1}
    };
    EXPECT_CALL(managerMock, validateTextureWrite(Ref(*manager), texture, _, bytes.size()))
        .WillOnce(
            Return(panda::core::vulkan::upload::TextureUploadLayout {.rowLength = 0, .bufferOffsetAlignment = 4}));
    EXPECT_CALL(textures, reserveUpload(Ref(*texture), _)).WillOnce(Return(panda::Result<void> {}));
    EXPECT_CALL(managerMock, collect(Ref(*manager)))
        .WillOnce(Return(panda::Result<void> {
            std::unexpected {panda::makeError(panda::ErrorCode::DeviceLost, "Controlled collection failure")}}));
    EXPECT_CALL(textures, cancelUpload(Ref(*texture), _));

    const auto result = batch->write(texture, bytes, region);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, panda::ErrorCode::DeviceLost);
}
