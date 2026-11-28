// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include "MeshResource.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <panda/base/Error.hpp>
#include <panda/resources/Mesh.hpp>

#include "ScopedMock.hpp"
#include "VulkanTestSupport.hpp"
#include "panda/core/BufferResourceMock.hpp"
#include "panda/core/MemoryPoolMock.hpp"
#include "panda/core/MeshLayoutMock.hpp"
#include "resources/BufferResource.hpp"
#include "resources/memory/MemoryPool.hpp"

namespace
{
using panda::core::vulkan::resources::MemoryPool;
using panda::detail::MeshResource;
using panda::test::MeshLayoutPlan;
using testing::_;
using testing::NotNull;
using testing::Ref;
using testing::Return;
using testing::Truly;

auto description(bool indexed = true) -> panda::MeshDesc
{
    return {.vertexCount = 3,
            .vertexStride = 12,
            .indices = indexed ? std::optional {panda::MeshIndexDesc {.count = 3, .type = panda::MeshIndexType::UInt16}}
                               : std::nullopt,
            .attributes = {}};
}

class MeshResourceTest : public panda::test::VulkanTestFixture
{
protected:
    void SetUp() override
    {
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceMemoryProperties(_, NotNull()))
            .WillOnce([](VkPhysicalDevice, VkPhysicalDeviceMemoryProperties* output) {
                ASSERT_NE(output, nullptr);
                output->memoryTypeCount = 1;
                output->memoryTypes[0] = {
                    .propertyFlags = static_cast<VkMemoryPropertyFlags>(vk::MemoryPropertyFlagBits::eDeviceLocal),
                    .heapIndex = 0};
                output->memoryHeapCount = 1;
                output->memoryHeaps[0] = {.size = 4096, .flags = 0};
            });
        EXPECT_CALL(vulkan, vkGetPhysicalDeviceProperties2(_, NotNull()))
            .WillOnce([](VkPhysicalDevice, VkPhysicalDeviceProperties2* output) {
                ASSERT_NE(output, nullptr);
                ASSERT_EQ(output->sType, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2);
                ASSERT_NE(output->pNext, nullptr);
                const auto* header = static_cast<const VkBaseOutStructure*>(output->pNext);
                ASSERT_EQ(header->sType, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES);
                output->properties.limits.maxMemoryAllocationCount = 8;
                auto* maintenance = static_cast<VkPhysicalDeviceMaintenance3Properties*>(output->pNext);
                ASSERT_NE(maintenance, nullptr);
                maintenance->maxMemoryAllocationSize = 4096;
            });
        EXPECT_CALL(poolMock, destroy(_));
        pool = MemoryPool::create(physical, device);
    }

    void expectLayout(const panda::MeshDesc& mesh, std::uint64_t vertexSize, std::uint64_t indexSize)
    {
        EXPECT_CALL(layoutMock, create(Truly([&mesh](const panda::MeshDesc& received) noexcept {
                        return received.vertexCount == mesh.vertexCount && received.vertexStride == mesh.vertexStride &&
                               received.indices.has_value() == mesh.indices.has_value();
        })))
            // GMock's WillOnce wrapper triggers a false-positive leak report in clang-analyzer.
            .WillRepeatedly(Return(
                panda::Result<MeshLayoutPlan> {MeshLayoutPlan {.vertexSize = vertexSize, .indexSize = indexSize}}));
    }

    panda::test::ScopedMock<panda::test::MemoryPoolMock> poolMock;
    panda::test::ScopedMock<panda::test::BufferResourceMock> buffers;
    panda::test::ScopedMock<panda::test::MeshLayoutMock> layoutMock;
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
    EXPECT_CALL(buffers,
                createNative(_, Ref(*pool), testing::Field(&panda::detail::BufferResourceCreateInfo::size, 36)))
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
    EXPECT_CALL(buffers,
                createNative(_, Ref(*pool), testing::Field(&panda::detail::BufferResourceCreateInfo::size, 36)))
        .WillOnce(Return(panda::Result<void> {}));
    EXPECT_CALL(buffers, createNative(_, Ref(*pool), testing::Field(&panda::detail::BufferResourceCreateInfo::size, 6)))
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
    EXPECT_CALL(buffers,
                createNative(_, Ref(*pool), testing::Field(&panda::detail::BufferResourceCreateInfo::size, 36)))
        .WillOnce(Return(panda::Result<void> {}));
    EXPECT_CALL(buffers, createNative(_, Ref(*pool), testing::Field(&panda::detail::BufferResourceCreateInfo::size, 6)))
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
    EXPECT_CALL(buffers,
                createNative(_, Ref(*pool), testing::Field(&panda::detail::BufferResourceCreateInfo::size, 36)))
        .WillOnce(Return(panda::Result<void> {}));

    const auto created = MeshResource::create(device, *pool, mesh);

    ASSERT_TRUE(created);
    EXPECT_NE((*created)->getVertexBuffer(), nullptr);
    EXPECT_EQ((*created)->getIndexBuffer(), nullptr);
}
