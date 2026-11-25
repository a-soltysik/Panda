#include "external/vulkan/VulkanMock.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <vulkan/vulkan_core.h>

#include "FakeHandle.hpp"
#include "ScopedMock.hpp"
#include "VulkanTestSupport.hpp"

TEST(VulkanMock, IndexedDrawForwardsThroughTheRegisteredEntryPoint)
{
    auto mock = panda::test::ScopedMock<panda::test::VulkanMock> {};
    auto* const commandBuffer = panda::test::fakeHandle<VkCommandBuffer>(1);
    const auto proc = vkGetDeviceProcAddr(panda::test::fakeHandle<VkDevice>(2), "vkCmdDrawIndexed");
    ASSERT_EQ(proc, panda::test::genericVulkanProc(&vkCmdDrawIndexed));

    EXPECT_CALL(mock, vkCmdDrawIndexed(commandBuffer, 6, 2, 4, -3, 1));
    vkCmdDrawIndexed(commandBuffer, 6, 2, 4, -3, 1);
}
