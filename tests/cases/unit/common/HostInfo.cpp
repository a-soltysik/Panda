#include <gtest/gtest.h>

#include <panda/HostInfo.hpp>

TEST(HostInfo, ReturnsNonEmptyCpuModelWhenAvailable)
{
    const auto info = panda::getHostInfo();

#if defined(_WIN32) || defined(__linux__)
    EXPECT_TRUE(info.cpuModel.has_value());
    EXPECT_FALSE(info.cpuModel.value_or("").empty());
#else
    if (info.cpuModel)
    {
        EXPECT_FALSE(info.cpuModel.value_or("").empty());
    }
#endif
}
