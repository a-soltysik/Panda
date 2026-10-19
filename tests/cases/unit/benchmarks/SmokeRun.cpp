#include "SmokeRun.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <expected>
#include <panda/PerformanceManager.hpp>
#include <panda/WindowSurface.hpp>
#include <string>
#include <thread>
#include <vector>

#include "SmokeRunBackend.hpp"
#include "panda_bench/Arguments.hpp"
#include "panda_bench/Report.hpp"

namespace
{
using std::chrono_literals::operator""ns;
using std::chrono_literals::operator""s;
using std::chrono_literals::operator""us;
using panda_bench::Failure;
using panda_bench::FrameStatus;
using panda_bench::Options;
using panda_bench::Report;
using panda_bench::RunReport;
using panda_bench::SmokeRun;
using panda_bench::SmokeRunBackend;
using panda_bench::SmokeRunEnvironment;
using testing::_;
using testing::AnyNumber;
using testing::Return;

// GMock generates interface methods that do not use Panda's trailing-return style.
// NOLINTBEGIN(modernize-use-trailing-return-type)
class MockSmokeRunBackend : public SmokeRunBackend
{
public:
    MOCK_METHOD((std::expected<SmokeRunEnvironment, Failure>), initialize, (const Options&), (override));
    MOCK_METHOD(bool, shouldClose, (), (const, override));
    MOCK_METHOD((std::expected<panda::FramebufferExtent, Failure>), getFramebufferExtent, (), (const, override));
    MOCK_METHOD((std::expected<FrameStatus, Failure>), submitFrame, (), (override));
    MOCK_METHOD((std::expected<std::vector<panda::FramePerformanceSample>, Failure>),
                takeAvailableSamples,
                (),
                (override));
    MOCK_METHOD((std::expected<void, Failure>), processInput, (), (override));
    MOCK_METHOD((std::expected<void, Failure>), waitForLastFrame, (std::chrono::nanoseconds), (override));
    MOCK_METHOD(void, teardown, (), (override));
};

// NOLINTEND(modernize-use-trailing-return-type)

class SmokeRunTest : public testing::Test
{
protected:
    void expectReadyBackend(panda::FramebufferExtent extent = {.width = 640, .height = 480})
    {
        EXPECT_CALL(backend, initialize(_))
            .Times(1)
            .WillRepeatedly(Return(std::expected<SmokeRunEnvironment, Failure> {
                SmokeRunEnvironment {.framebufferExtent = extent,
                                     .gpuModel = "Test GPU",
                                     .driver = {},
                                     .vulkanApiVersion = {}}
        }));
        EXPECT_CALL(backend, shouldClose()).Times(AnyNumber()).WillRepeatedly(Return(false));
        EXPECT_CALL(backend, getFramebufferExtent())
            .Times(AnyNumber())
            .WillRepeatedly(Return(std::expected<panda::FramebufferExtent, Failure> {extent}));
        EXPECT_CALL(backend, submitFrame()).Times(AnyNumber()).WillRepeatedly(Return(FrameStatus::Presented));
        EXPECT_CALL(backend, takeAvailableSamples())
            .Times(AnyNumber())
            .WillRepeatedly(Return(std::expected<std::vector<panda::FramePerformanceSample>, Failure> {
                std::vector<panda::FramePerformanceSample> {}}));
        EXPECT_CALL(backend, processInput()).Times(AnyNumber()).WillRepeatedly(Return(std::expected<void, Failure> {}));
        EXPECT_CALL(backend, waitForLastFrame(_))
            .Times(AnyNumber())
            .WillRepeatedly(Return(std::expected<void, Failure> {}));
        EXPECT_CALL(backend, teardown()).Times(1);
    }

    auto execute() -> std::expected<void, Failure>
    {
        return SmokeRun {options, run, report, backend}.execute();
    }

    Options options {.warmupDuration = 0s, .measurementDuration = 1s};
    RunReport run {};
    Report report {};
    testing::StrictMock<MockSmokeRunBackend> backend;
};
}

TEST_F(SmokeRunTest, InitializationFailureIsPropagatedAndTornDown)
{
    EXPECT_CALL(backend, initialize(_))
        .WillOnce(Return(std::expected<SmokeRunEnvironment, Failure> {
            std::unexpected {Failure {.outcome = "unsupported", .message = "No timestamp support"}}}));
    EXPECT_CALL(backend, teardown());

    const auto result = execute();

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().outcome, "unsupported");
    EXPECT_EQ(result.error().message, "No timestamp support");
    EXPECT_GE(run.startupDuration.count(), 0);
    EXPECT_GE(run.teardownDuration.count(), 0);
}

TEST_F(SmokeRunTest, RejectsAZeroSizedInitialFramebuffer)
{
    expectReadyBackend({.width = 0, .height = 480});

    const auto result = execute();

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().outcome, "invalid");
    EXPECT_EQ(result.error().message, "The benchmark window is minimized");
    EXPECT_EQ(run.measurementSubmittedFrames, 0U);
}

TEST_F(SmokeRunTest, StopsWhenTheWindowIsClosed)
{
    expectReadyBackend();
    EXPECT_CALL(backend, shouldClose()).WillOnce(Return(true));

    const auto result = execute();

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().message, "The benchmark window was closed before the run completed");
    EXPECT_EQ(run.measurementSubmittedFrames, 0U);
}

TEST_F(SmokeRunTest, RejectsAFramebufferResizeDuringMeasurement)
{
    expectReadyBackend();
    EXPECT_CALL(backend, getFramebufferExtent())
        .WillOnce(Return(std::expected<panda::FramebufferExtent, Failure> {
            {.width = 640, .height = 480}
    }))
        .WillOnce(Return(std::expected<panda::FramebufferExtent, Failure> {{.width = 800, .height = 600}}));

    const auto result = execute();

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().outcome, "invalid");
    EXPECT_EQ(result.error().message, "The framebuffer extent changed during the run");
    EXPECT_EQ(run.measurementSubmittedFrames, 1U);
}

TEST_F(SmokeRunTest, RejectsFramesThatWereNotPresented)
{
    expectReadyBackend();
    EXPECT_CALL(backend, submitFrame()).WillOnce(Return(FrameStatus::Skipped));

    const auto result = execute();

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().outcome, "invalid");
    EXPECT_EQ(result.error().message, "The frame was skipped during the controlled run");
    EXPECT_EQ(run.measurementSubmittedFrames, 0U);
}

TEST_F(SmokeRunTest, PropagatesSampleReadbackFailure)
{
    expectReadyBackend();
    EXPECT_CALL(backend, takeAvailableSamples())
        .WillOnce(Return(std::expected<std::vector<panda::FramePerformanceSample>, Failure> {
            std::unexpected {Failure {.message = "Sample readback failed"}}}));

    const auto result = execute();

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().message, "Sample readback failed");
    EXPECT_EQ(run.measurementSubmittedFrames, 1U);
}

TEST_F(SmokeRunTest, PropagatesWindowEventFailure)
{
    expectReadyBackend();
    EXPECT_CALL(backend, processInput())
        .WillOnce(Return(std::expected<void, Failure> {std::unexpected {Failure {.message = "Event failure"}}}));

    const auto result = execute();

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().message, "Event failure");
    EXPECT_EQ(run.measurementSubmittedFrames, 1U);
}

TEST_F(SmokeRunTest, RecordsPresentedFrameSamplesAndWaitDurations)
{
    expectReadyBackend();
    const auto sample =
        panda::FramePerformanceSample {.frameIndex = 0, .cpuFrameDuration = 250us, .gpuExecutionDuration = 150us};
    const auto sampleAfterMeasurement = panda::FramePerformanceSample {.frameIndex = 1};
    EXPECT_CALL(backend, takeAvailableSamples())
        .WillOnce(Return(std::expected<std::vector<panda::FramePerformanceSample>, Failure> {
            std::vector {sample, sampleAfterMeasurement}
    }))
        .WillOnce(Return(
            std::expected<std::vector<panda::FramePerformanceSample>, Failure> {std::vector {sampleAfterMeasurement}}));
    EXPECT_CALL(backend, processInput()).WillOnce([] {
        std::this_thread::sleep_for(1s);
        return std::expected<void, Failure> {};
    });
    EXPECT_CALL(backend, waitForLastFrame(30'000'000'000ns)).WillOnce(Return(std::expected<void, Failure> {}));

    const auto result = execute();

    ASSERT_TRUE(result);
    EXPECT_EQ(run.framebufferWidth, 640U);
    EXPECT_EQ(run.framebufferHeight, 480U);
    EXPECT_EQ(run.measurementSubmittedFrames, 1U);
    ASSERT_EQ(run.frameSamples.size(), 1U);
    EXPECT_EQ(run.frameSamples.front().frameIndex, 0U);
    EXPECT_EQ(run.frameSamples.front().cpuFrameDuration, 250us);
    EXPECT_EQ(run.frameSamples.front().gpuExecutionDuration, 150us);
    EXPECT_EQ(report.gpuModel, "Test GPU");
    EXPECT_GE(run.completionWaitDuration, 0ns);
    EXPECT_GE(run.sampleDrainDuration, 0ns);
}
