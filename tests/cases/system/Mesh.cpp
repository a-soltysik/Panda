#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <panda/Assert.hpp>
#include <panda/CompletionPoint.hpp>
#include <panda/Context.hpp>
#include <panda/Error.hpp>
#include <panda/Logger.hpp>
#include <panda/Mesh.hpp>
#include <panda/UploadBatch.hpp>
#include <span>
#include <utility>

#include "LogCapture.hpp"

namespace
{
const auto vertices = std::array {
    std::array {0.0F, 0.0F, 0.0F},
     std::array {1.0F, 0.0F, 0.0F},
     std::array {0.0F, 1.0F, 0.0F}
};
const auto indices = std::array<std::uint16_t, 3> {0, 1, 2};
const auto vertexBytes = std::as_bytes(std::span {vertices});
const auto indexBytes = std::as_bytes(std::span {indices});

auto indexedDescription() -> panda::MeshDesc
{
    return {
        .vertexCount = static_cast<std::uint32_t>(vertices.size()),
        .vertexStride = static_cast<std::uint32_t>(sizeof(vertices.front())),
        .indices = panda::MeshIndexDesc {.count = static_cast<std::uint32_t>(indices.size()),
                                         .type = panda::MeshIndexType::UInt16}
    };
}

auto nonIndexedDescription() -> panda::MeshDesc
{
    return {.vertexCount = static_cast<std::uint32_t>(vertices.size()),
            .vertexStride = static_cast<std::uint32_t>(sizeof(vertices.front())),
            .indices = std::nullopt};
}

class MeshSystem : public testing::Test
{
protected:
    void SetUp() override
    {
        auto created = panda::Context::create({.applicationName = "Panda mesh smoke", .enableValidation = true});
        if (!created && created.error().code == panda::ErrorCode::Unsupported)
        {
            GTEST_SKIP() << created.error().message;
        }
        ASSERT_TRUE(created) << created.error().message;
        context.emplace(std::move(*created));
    }

    void TearDown() override
    {
        context.reset();
        const auto error = std::ranges::find_if(records.entries, [](const auto& entry) {
            return entry.level == panda::log::Level::Error;
        });
        EXPECT_EQ(error, records.entries.end()) << (error == records.entries.end() ? "" : error->message);
    }

    auto activeContext() -> panda::Context&
    {
        if (!context)
        {
            panda::panic("Mesh system test has no initialized Context");
        }
        return *context;
    }

    void uploadAndWait(panda::Mesh& mesh, std::span<const std::byte> indicesToUpload = {})
    {
        auto batch = activeContext().createUploadBatch();
        ASSERT_TRUE(batch) << batch.error().message;
        ASSERT_TRUE(batch->write(mesh, vertexBytes, indicesToUpload));
        const auto submitted = batch->submit();
        ASSERT_TRUE(submitted) << submitted.error().message;
        mesh.reset();
        const auto waited = activeContext().wait(*submitted, std::chrono::seconds {10});
        ASSERT_TRUE(waited) << waited.error().message;
        EXPECT_EQ(*waited, panda::WaitStatus::Completed);
    }

    panda::test::LogRecords records;
    panda::test::ProcessSinkRegistration capture {records};
    std::optional<panda::Context> context;
};
}

TEST_F(MeshSystem, UploadsIndexedMeshAndRetainsItsBuffersThroughCompletion)
{
    auto created = activeContext().createMesh(indexedDescription());
    if (!created && created.error().code == panda::ErrorCode::Unsupported)
    {
        GTEST_SKIP() << created.error().message;
    }
    ASSERT_TRUE(created) << created.error().message;
    auto mesh = std::move(*created);
    EXPECT_EQ(mesh.getVertexCount(), indexedDescription().vertexCount);
    EXPECT_EQ(mesh.getVertexStride(), indexedDescription().vertexStride);
    EXPECT_EQ(mesh.getVertexSize(), vertexBytes.size());
    EXPECT_EQ(mesh.getIndexCount(), indices.size());
    EXPECT_EQ(mesh.getIndexType(), std::optional {panda::MeshIndexType::UInt16});
    EXPECT_EQ(mesh.getIndexSize(), indexBytes.size());

    uploadAndWait(mesh, indexBytes);

    EXPECT_FALSE(mesh.isValid());
}

TEST_F(MeshSystem, UploadsNonIndexedMeshWithoutIndexBytes)
{
    auto created = activeContext().createMesh(nonIndexedDescription());
    ASSERT_TRUE(created) << created.error().message;
    auto mesh = std::move(*created);
    EXPECT_EQ(mesh.getVertexCount(), nonIndexedDescription().vertexCount);
    EXPECT_EQ(mesh.getVertexSize(), vertexBytes.size());
    EXPECT_EQ(mesh.getIndexCount(), 0);
    EXPECT_FALSE(mesh.getIndexType());
    EXPECT_EQ(mesh.getIndexSize(), 0);

    uploadAndWait(mesh);

    EXPECT_FALSE(mesh.isValid());
}
