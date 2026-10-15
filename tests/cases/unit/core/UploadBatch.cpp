#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <panda/Buffer.hpp>
#include <panda/Error.hpp>
#include <panda/UploadBatch.hpp>
#include <utility>

TEST(UploadBatch, EmptyHandleRejectsWriteAndSubmit)
{
    auto batch = panda::UploadBatch {};
    const auto target = panda::Buffer {};
    const auto bytes = std::array {std::byte {1}};
    EXPECT_FALSE(batch.isValid());
    const auto written = batch.write(target, 0, bytes);
    ASSERT_FALSE(written);
    EXPECT_EQ(written.error().code, panda::ErrorCode::InvalidArgument);
    const auto submitted = batch.submit();
    ASSERT_FALSE(submitted);
    EXPECT_EQ(submitted.error().code, panda::ErrorCode::InvalidArgument);
}

TEST(UploadBatch, EmptyHandleRejectsTextureWrites)
{
    auto batch = panda::UploadBatch {};
    const auto target = panda::Texture {};
    const auto bytes = std::array {std::byte {1}};
    const auto region = panda::TextureUploadRegion {};

    const auto fullImageWrite = batch.write(target, bytes);
    ASSERT_FALSE(fullImageWrite);
    EXPECT_EQ(fullImageWrite.error().code, panda::ErrorCode::InvalidArgument);

    const auto regionWrite = batch.write(target, bytes, region);
    ASSERT_FALSE(regionWrite);
    EXPECT_EQ(regionWrite.error().code, panda::ErrorCode::InvalidArgument);
    EXPECT_FALSE(batch.isValid());
}

TEST(UploadBatch, MovingAndReplacingEmptyHandlesPreservesInvalidState)
{
    auto original = panda::UploadBatch {};
    auto moved = std::move(original);
    // The wrapper intentionally supports querying an empty moved-from handle.
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move,hicpp-invalid-access-moved)
    EXPECT_FALSE(original.isValid());
    EXPECT_FALSE(moved.isValid());
    original = std::move(moved);
    EXPECT_FALSE(original.isValid());
    // The wrapper intentionally supports querying an empty moved-from handle.
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move,hicpp-invalid-access-moved)
    EXPECT_FALSE(moved.isValid());
    EXPECT_FALSE(original.submit());
    EXPECT_FALSE(moved.submit());
}
