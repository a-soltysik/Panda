// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
// clang-format on

#include "MeshResource.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <memory>
#include <panda/Error.hpp>
#include <panda/Mesh.hpp>
#include <tuple>
#include <utility>

#include "ScopedMock.hpp"
#include "VulkanTestSupport.hpp"
#include "external/vulkan/VulkanMock.hpp"
#include "panda/core/BufferResourceMock.hpp"
#include "panda/core/MemoryAllocationMock.hpp"
#include "panda/core/MemoryPoolMock.hpp"
#include "panda/core/MeshLayoutMock.hpp"

namespace
{
using panda::core::vulkan::resources::MemoryPool;
using panda::detail::MeshResource;
using panda::test::fakeVulkanHandle;
using panda::test::MeshLayoutPlan;
using testing::_;
using testing::NotNull;
using testing::Ref;
using testing::Return;

auto description(bool indexed = true) -> panda::MeshDesc
{
    return {.vertexCount = 3,
            .vertexStride = 12,
            .indices = indexed ? std::optional {panda::MeshIndexDesc {.count = 3, .type = panda::MeshIndexType::UInt16}}
                               : std::nullopt};
}

class MeshResourceTest : public testing::Test
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
        EXPECT_CALL(poolMock, destroy(_));
        pool = MemoryPool::create(physical, device);
    }

    void TearDown() override
    {
        pool.reset();
        std::ignore = device.release();
        std::ignore = instance.release();
    }

    void expectLayout(const panda::MeshDesc& mesh, std::uint64_t vertexSize, std::uint64_t indexSize)
    {
        EXPECT_CALL(layoutMock, create(_)).WillOnce([mesh, vertexSize, indexSize](const panda::MeshDesc& received) {
            EXPECT_EQ(received.vertexCount, mesh.vertexCount);
            EXPECT_EQ(received.vertexStride, mesh.vertexStride);
            EXPECT_EQ(received.indices.has_value(), mesh.indices.has_value());
            return panda::Result<MeshLayoutPlan> {
                MeshLayoutPlan {.description = received, .vertexSize = vertexSize, .indexSize = indexSize}
            };
        });
    }

    panda::test::ScopedMock<panda::test::VulkanMock> vulkan;
    panda::test::ScopedMock<panda::test::MemoryPoolMock> poolMock;
    panda::test::ScopedMock<panda::test::MemoryAllocationMock> allocationMock;
    panda::test::ScopedMock<panda::test::BufferResourceMock> buffers;
    panda::test::ScopedMock<panda::test::MeshLayoutMock> layoutMock;
    vk::raii::Context loader {vkGetInstanceProcAddr};
    vk::raii::Instance instance {loader, fakeVulkanHandle<VkInstance>(1)};
    vk::raii::PhysicalDevice physical {instance, fakeVulkanHandle<VkPhysicalDevice>(2)};
    vk::raii::Device device {physical, fakeVulkanHandle<VkDevice>(3)};
    std::unique_ptr<MemoryPool> pool;
};
}

TEST_F(MeshResourceTest, PropagatesLayoutFailureWithoutCreatingBuffers)
{
    const auto mesh = description();
    EXPECT_CALL(layoutMock, create(_))
        .WillOnce(Return(panda::Result<MeshLayoutPlan> {
            std::unexpected {panda::makeError(panda::ErrorCode::InvalidArgument, "Controlled layout failure")}}));

    const auto created = MeshResource::create(device, *pool, mesh);

    ASSERT_FALSE(created);
    EXPECT_EQ(created.error().code, panda::ErrorCode::InvalidArgument);
}

TEST_F(MeshResourceTest, PropagatesVertexBufferFailure)
{
    const auto mesh = description();
    expectLayout(mesh, 36, 6);
    EXPECT_CALL(layoutMock, getVertexSize(_)).WillOnce(Return(36));
    EXPECT_CALL(buffers, create(_, Ref(*pool), testing::Field(&panda::BufferDesc::size, 36)))
        .WillOnce(Return(panda::Result<void> {
            std::unexpected {panda::makeError(panda::ErrorCode::ResourceExhausted, "Controlled vertex failure")}}));

    const auto created = MeshResource::create(device, *pool, mesh);

    ASSERT_FALSE(created);
    EXPECT_EQ(created.error().code, panda::ErrorCode::ResourceExhausted);
}

TEST_F(MeshResourceTest, ReleasesTheVertexBufferWhenIndexBufferCreationFails)
{
    const auto mesh = description();
    expectLayout(mesh, 36, 6);
    EXPECT_CALL(layoutMock, getVertexSize(_)).WillOnce(Return(36));
    EXPECT_CALL(layoutMock, getIndexSize(_)).WillRepeatedly(Return(6));
    EXPECT_CALL(buffers, create(_, Ref(*pool), testing::Field(&panda::BufferDesc::size, 36)))
        .WillOnce(Return(panda::Result<void> {}));
    EXPECT_CALL(buffers, create(_, Ref(*pool), testing::Field(&panda::BufferDesc::size, 6)))
        .WillOnce(Return(panda::Result<void> {
            std::unexpected {panda::makeError(panda::ErrorCode::ResourceExhausted, "Controlled index failure")}}));

    const auto created = MeshResource::create(device, *pool, mesh);

    ASSERT_FALSE(created);
    EXPECT_EQ(created.error().code, panda::ErrorCode::ResourceExhausted);
}

TEST_F(MeshResourceTest, CreatesAndRetainsTheDescribedBuffers)
{
    const auto mesh = description();
    expectLayout(mesh, 36, 6);
    EXPECT_CALL(layoutMock, getVertexSize(_)).WillOnce(Return(36));
    EXPECT_CALL(layoutMock, getIndexSize(_)).WillRepeatedly(Return(6));
    EXPECT_CALL(buffers, create(_, Ref(*pool), testing::Field(&panda::BufferDesc::size, 36)))
        .WillOnce(Return(panda::Result<void> {}));
    EXPECT_CALL(buffers, create(_, Ref(*pool), testing::Field(&panda::BufferDesc::size, 6)))
        .WillOnce(Return(panda::Result<void> {}));
    EXPECT_CALL(layoutMock, getVertexCount(_)).WillOnce(Return(3));

    auto created = MeshResource::create(device, *pool, mesh);

    ASSERT_TRUE(created);
    EXPECT_NE((*created)->getVertexBuffer(), nullptr);
    EXPECT_NE((*created)->getIndexBuffer(), nullptr);
    EXPECT_EQ((*created)->getLayout().getVertexCount(), 3);
}

TEST_F(MeshResourceTest, CreatesOnlyAVertexBufferForNonIndexedMeshes)
{
    const auto mesh = description(false);
    expectLayout(mesh, 36, 0);
    EXPECT_CALL(layoutMock, getVertexSize(_)).WillOnce(Return(36));
    EXPECT_CALL(layoutMock, getIndexSize(_)).WillOnce(Return(0));
    EXPECT_CALL(buffers, create(_, Ref(*pool), testing::Field(&panda::BufferDesc::size, 36)))
        .WillOnce(Return(panda::Result<void> {}));

    const auto created = MeshResource::create(device, *pool, mesh);

    ASSERT_TRUE(created);
    EXPECT_NE((*created)->getVertexBuffer(), nullptr);
    EXPECT_EQ((*created)->getIndexBuffer(), nullptr);
}
