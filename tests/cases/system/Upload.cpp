// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <panda/Assert.hpp>
#include <panda/Buffer.hpp>
#include <panda/CompletionPoint.hpp>
#include <panda/Context.hpp>
#include <panda/Error.hpp>
#include <panda/Logger.hpp>
#include <panda/UploadBatch.hpp>
#include <span>
#include <utility>
#include <vector>

#include "LogCapture.hpp"
#include "resources/BufferResource.hpp"

namespace
{
class UploadSystem : public testing::Test
{
protected:
    void SetUp() override
    {
        auto created = panda::Context::create({.applicationName = "Panda upload smoke", .enableValidation = true});
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
        EXPECT_FALSE(std::ranges::any_of(records.entries, [](const auto& entry) {
            return entry.level == panda::log::Level::Error;
        }));
    }

    auto makeDestination(panda::BufferMemory intent = panda::BufferMemory::Readback) -> panda::Buffer
    {
        auto buffer = activeContext().createBuffer(
            {.size = 512, .usage = vk::BufferUsageFlagBits::eTransferDst, .memory = intent});
        return panda::expect(std::move(buffer), "Test upload destination creation failed");
    }

    void finish(panda::CompletionPoint point)
    {
        const auto waited = activeContext().wait(point, std::chrono::seconds {10});
        ASSERT_TRUE(waited) << waited.error().message;
        ASSERT_EQ(*waited, panda::WaitStatus::Completed);
        const auto polled = activeContext().poll(point);
        ASSERT_TRUE(polled);
        EXPECT_TRUE(*polled);
    }

    static void verifyBytes(const panda::Buffer& target, std::uint64_t offset, std::span<const std::byte> expected)
    {
        const auto& resource = panda::detail::BufferResource::getResource(target);
        const auto invalidated = resource->invalidate(offset, expected.size());
        ASSERT_TRUE(invalidated) << invalidated.error().message;
        const auto mapped = resource->getMappedBytes();
        ASSERT_TRUE(mapped);
        const auto actual = mapped->subspan(static_cast<std::size_t>(offset), expected.size());
        EXPECT_TRUE(std::ranges::equal(actual, expected));
    }

    auto activeContext() -> panda::Context&
    {
        if (!context)
        {
            panda::panic("Upload system test has no initialized Context");
        }
        return *context;
    }

    panda::test::LogRecords records;
    panda::test::ProcessSinkRegistration capture {records};
    std::optional<panda::Context> context;
};
}

TEST_F(UploadSystem, CopiesByteRangesAndPreservesSnapshotAfterCallerChangesData)
{
    auto target = makeDestination();
    auto batch = activeContext().createUploadBatch();
    ASSERT_TRUE(batch);
    auto bytes = std::vector<std::byte>(257, std::byte {0x5a});
    const auto expected = bytes;
    ASSERT_TRUE(batch->write(target, 3, bytes));
    bytes.assign(bytes.size(), std::byte {0});
    const auto submitted = batch->submit();
    ASSERT_TRUE(submitted) << submitted.error().message;
    EXPECT_FALSE(batch->isValid());
    finish(*submitted);
    verifyBytes(target, 3, expected);
}

TEST_F(UploadSystem, OverlappingWritesAndLaterBatchesFollowSubmissionOrder)
{
    auto target = makeDestination();
    auto initial = activeContext().createUploadBatch();
    ASSERT_TRUE(initial);
    const auto zeros = std::array<std::byte, 16> {};
    const auto first = std::array {std::byte {1}, std::byte {2}, std::byte {3}, std::byte {4}};
    const auto second = std::array {std::byte {7}, std::byte {8}, std::byte {9}};
    ASSERT_TRUE(initial->write(target, 0, zeros));
    ASSERT_TRUE(initial->write(target, 3, first));
    ASSERT_TRUE(initial->write(target, 5, second));
    const auto initialPoint = initial->submit();
    ASSERT_TRUE(initialPoint);
    auto later = activeContext().createUploadBatch();
    ASSERT_TRUE(later);
    const auto final = std::array {std::byte {0x42}};
    ASSERT_TRUE(later->write(target, 4, final));
    const auto last = later->submit();
    ASSERT_TRUE(last);
    auto expected = zeros;
    const auto expectedBytes = std::span {expected};
    std::ranges::copy(first, expectedBytes.subspan(3).begin());
    std::ranges::copy(final, expectedBytes.subspan(4).begin());
    std::ranges::copy(second, expectedBytes.subspan(5).begin());
    finish(*last);
    verifyBytes(target, 0, expected);
}

TEST_F(UploadSystem, SubmittedOwnerSurvivesResetAndIsReleasedAfterCompletion)
{
    auto target = makeDestination(panda::BufferMemory::Device);
    const auto weak = std::weak_ptr {panda::detail::BufferResource::getResource(target)};
    auto batch = activeContext().createUploadBatch();
    ASSERT_TRUE(batch);
    const auto bytes = std::array {std::byte {1}, std::byte {2}, std::byte {3}};
    ASSERT_TRUE(batch->write(target, 1, bytes));
    target.reset();
    const auto point = batch->submit();
    ASSERT_TRUE(point);
    EXPECT_FALSE(weak.expired());
    finish(*point);
    EXPECT_TRUE(weak.expired());
}

TEST_F(UploadSystem, DestructionDrainsPendingUploadsAfterApplicationOwnersAreGone)
{
    auto target = makeDestination(panda::BufferMemory::Device);
    const auto weak = std::weak_ptr {panda::detail::BufferResource::getResource(target)};
    auto batch = activeContext().createUploadBatch();
    ASSERT_TRUE(batch);
    const auto bytes = std::array {std::byte {1}, std::byte {2}};
    ASSERT_TRUE(batch->write(target, 0, bytes));
    ASSERT_TRUE(batch->submit());
    target.reset();
    EXPECT_FALSE(weak.expired());
    context.reset();
    EXPECT_TRUE(weak.expired());
}

TEST_F(UploadSystem, BatchAndPointRemainUsableAfterMovingContext)
{
    auto target = makeDestination();
    auto batch = activeContext().createUploadBatch();
    ASSERT_TRUE(batch);
    auto moved = std::move(activeContext());
    const auto bytes = std::array {std::byte {4}, std::byte {5}, std::byte {6}};
    ASSERT_TRUE(batch->write(target, 3, bytes));
    const auto point = batch->submit();
    ASSERT_TRUE(point);
    const auto waited = moved.wait(*point, std::chrono::seconds {10});
    ASSERT_TRUE(waited);
    ASSERT_EQ(*waited, panda::WaitStatus::Completed);
    verifyBytes(target, 3, bytes);
    target.reset();
}
