#include <gtest/gtest.h>

#include <panda/cuda/Cuda.hpp>

TEST(CudaAvailabilityIntegration, StartsAndReportsRuntimeAvailability)
{
    const auto result = panda::cuda::queryAvailability();
    ASSERT_TRUE(result.has_value()) << "CUDA Runtime status: " << result.error().nativeCode;

    const auto availability = *result;
    EXPECT_TRUE(availability == panda::cuda::Availability::Available ||
                availability == panda::cuda::Availability::NoDriver ||
                availability == panda::cuda::Availability::NoDevice);
}
