// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
// clang-format on

#include "MemoryAllocation.hpp"

#include <gtest/gtest.h>
#include <vulkan/vk_platform.h>
#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <iostream>
#include <iterator>
#include <optional>
#include <panda/Error.hpp>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include "MemoryBlock.hpp"
#include "MemoryPool.hpp"
#include "RangeAllocator.hpp"
#include "VulkanError.hpp"

namespace
{
auto makeTestBuffer(const vk::raii::Device& device) -> panda::Result<vk::raii::Buffer>
{
    const auto info = vk::BufferCreateInfo {.size = 257,
                                            .usage = vk::BufferUsageFlagBits::eTransferSrc,
                                            .sharingMode = vk::SharingMode::eExclusive};
    auto native = vk::Buffer {};
    const auto result = (*device).createBuffer(&info, nullptr, &native, *device.getDispatcher());
    if (result != vk::Result::eSuccess)
    {
        return std::unexpected {panda::detail::makeVulkanError(result)};
    }
    return panda::Result<vk::raii::Buffer> {std::in_place, device, static_cast<VkBuffer>(native)};
}

VKAPI_ATTR auto VKAPI_CALL countValidation(vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
                                           [[maybe_unused]] vk::DebugUtilsMessageTypeFlagsEXT type,
                                           const vk::DebugUtilsMessengerCallbackDataEXT* data,
                                           void* userData) noexcept -> vk::Bool32
{
    if (severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eError)
    {
        ++*static_cast<std::atomic<std::uint32_t>*>(userData);
        std::ignore = std::fputs(data->pMessage, stderr);
        std::ignore = std::fputc('\n', stderr);
    }
    return vk::False;
}

class MemoryAllocationSystem : public testing::TestWithParam<bool>
{
protected:
    void SetUp() override
    {
        createInstance();
        if (IsSkipped() || HasFatalFailure())
        {
            return;
        }
        selectDevice();
    }

    void TearDown() override
    {
        device.clear();
        messenger.clear();
        instance.clear();
        EXPECT_EQ(validationErrors.load(), 0);
    }

    void createInstance()
    {
        const auto layers = loader.enumerateInstanceLayerProperties();
        ASSERT_EQ(layers.result, vk::Result::eSuccess);
        const auto available = std::ranges::any_of(layers.value, [](const auto& layer) {
            return std::string_view {layer.layerName.data()} == "VK_LAYER_KHRONOS_validation";
        });
        if (!available)
        {
            GTEST_SKIP() << "VK_LAYER_KHRONOS_validation is unavailable";
        }
        static constexpr auto layer = "VK_LAYER_KHRONOS_validation";
        static constexpr auto extension = "VK_EXT_debug_utils";
        const auto application =
            vk::ApplicationInfo {.pApplicationName = "Panda memory block smoke", .apiVersion = vk::ApiVersion13};
        const auto info = vk::InstanceCreateInfo {.pApplicationInfo = &application,
                                                  .enabledLayerCount = 1,
                                                  .ppEnabledLayerNames = &layer,
                                                  .enabledExtensionCount = 1,
                                                  .ppEnabledExtensionNames = &extension};
        auto native = vk::Instance {};
        ASSERT_EQ(vk::createInstance(&info, nullptr, &native, *loader.getDispatcher()), vk::Result::eSuccess);
        instance = vk::raii::Instance {loader, static_cast<VkInstance>(native)};
        createMessenger();
    }

    void createMessenger()
    {
        const auto info =
            vk::DebugUtilsMessengerCreateInfoEXT {.messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
                                                  .messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                                                                 vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation,
                                                  .pfnUserCallback = countValidation,
                                                  .pUserData = &validationErrors};
        auto native = vk::DebugUtilsMessengerEXT {};
        ASSERT_EQ((*instance).createDebugUtilsMessengerEXT(&info, nullptr, &native, *instance.getDispatcher()),
                  vk::Result::eSuccess);
        messenger = vk::raii::DebugUtilsMessengerEXT {instance, static_cast<VkDebugUtilsMessengerEXT>(native)};
    }

    void selectDevice()
    {
        auto listed = instance.enumeratePhysicalDevices();
        ASSERT_EQ(listed.result, vk::Result::eSuccess);
        if (listed.value.empty())
        {
            GTEST_SKIP() << "No Vulkan physical device";
        }
        physical = std::move(listed.value.front());
        if (physical.getProperties().apiVersion < vk::ApiVersion13)
        {
            GTEST_SKIP() << "First physical device does not support Vulkan 1.3";
        }
        const auto families = physical.getQueueFamilyProperties();
        const auto found = std::ranges::find_if(families, [](const auto& family) {
            return family.queueCount != 0;
        });
        ASSERT_NE(found, families.end());
        const auto family = static_cast<std::uint32_t>(std::distance(families.begin(), found));
        static constexpr auto priority = 1.0F;
        const auto queue =
            vk::DeviceQueueCreateInfo {.queueFamilyIndex = family, .queueCount = 1, .pQueuePriorities = &priority};
        const auto info = vk::DeviceCreateInfo {.queueCreateInfoCount = 1, .pQueueCreateInfos = &queue};
        auto created = panda::detail::checkedCreation(physical.createDevice(info));
        ASSERT_TRUE(created) << created.error().message;
        device = std::move(*created);
    }

    auto findHostMemory(std::uint32_t bits) -> std::optional<std::uint32_t>
    {
        const auto properties = physical.getMemoryProperties();
        for (auto index = std::uint32_t {0}; index < properties.memoryTypeCount; ++index)
        {
            const auto flags = properties.memoryTypes.at(index).propertyFlags;
            const auto coherent = static_cast<bool>(flags & vk::MemoryPropertyFlagBits::eHostCoherent);
            if ((bits & (std::uint32_t {1} << index)) != 0 && (flags & vk::MemoryPropertyFlagBits::eHostVisible) &&
                coherent == GetParam())
            {
                return index;
            }
        }
        return std::nullopt;
    }

    std::atomic<std::uint32_t> validationErrors {0};
    vk::raii::Context loader {vkGetInstanceProcAddr};
    vk::raii::Instance instance {nullptr};
    vk::raii::DebugUtilsMessengerEXT messenger {nullptr};
    vk::raii::PhysicalDevice physical {nullptr};
    vk::raii::Device device {nullptr};
};

void verifyMappedSlices(const panda::detail::MemoryBlock& block,
                        panda::detail::MemoryRange first,
                        panda::detail::MemoryRange second)
{
    const auto firstBytes = block.getMappedBytes(first);
    const auto secondBytes = block.getMappedBytes(second);
    ASSERT_TRUE(firstBytes);
    ASSERT_TRUE(secondBytes);
    std::ranges::fill(*firstBytes, std::byte {0x2A});
    std::ranges::fill(*secondBytes, std::byte {0x5B});
    ASSERT_TRUE(block.flush(first, 0, first.size));
    ASSERT_TRUE(block.flush(second, 0, second.size));
    ASSERT_TRUE(block.invalidate(first, 0, first.size));
    EXPECT_TRUE(std::ranges::all_of(*firstBytes, [](std::byte value) {
        return value == std::byte {0x2A};
    }));
    EXPECT_TRUE(std::ranges::all_of(*secondBytes, [](std::byte value) {
        return value == std::byte {0x5B};
    }));
}

void verifyBufferBindings(panda::detail::MemoryBlock& block,
                          vk::raii::Buffer& firstBuffer,
                          vk::raii::Buffer& secondBuffer,
                          const vk::MemoryRequirements& required)
{
    const auto first = block.allocate(required.size, required.alignment);
    const auto second = block.allocate(required.size, required.alignment);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    ASSERT_EQ(firstBuffer.bindMemory(block.getMemory(), first->offset), vk::Result::eSuccess);
    ASSERT_EQ(secondBuffer.bindMemory(block.getMemory(), second->offset), vk::Result::eSuccess);
    verifyMappedSlices(block, *first, *second);
    firstBuffer.clear();
    secondBuffer.clear();
    ASSERT_TRUE(block.release(*first));
    ASSERT_TRUE(block.release(*second));
}

auto allocateForBuffer(panda::detail::MemoryPool& pool,
                       const vk::raii::Device& device,
                       const vk::raii::Buffer& buffer,
                       std::uint32_t type,
                       bool forceDedicated = false) -> panda::Result<panda::detail::MemoryAllocation>
{
    const auto queried = device.getBufferMemoryRequirements2<vk::MemoryRequirements2, vk::MemoryDedicatedRequirements>(
        vk::BufferMemoryRequirementsInfo2 {.buffer = *buffer});
    auto request =
        panda::detail::MemoryRequest {.requirements = queried.get<vk::MemoryRequirements2>().memoryRequirements,
                                      .dedicated = queried.get<vk::MemoryDedicatedRequirements>(),
                                      .resource = {.buffer = *buffer},
                                      .requiredProperties = vk::MemoryPropertyFlagBits::eHostVisible,
                                      .preferredProperties = {}};
    request.requirements.memoryTypeBits &= std::uint32_t {1} << type;
    if (forceDedicated)
    {
        request.dedicated.requiresDedicatedAllocation = vk::True;
    }
    auto allocation = pool.allocate(request);
    if (!allocation)
    {
        return allocation;
    }
    const auto bound = buffer.bindMemory(allocation->getMemory(), allocation->getRange().offset);
    if (bound != vk::Result::eSuccess)
    {
        return std::unexpected {panda::detail::makeVulkanError(bound)};
    }
    return allocation;
}

auto requiresDedicated(const vk::raii::Device& device, const vk::raii::Buffer& buffer) -> bool
{
    const auto queried = device.getBufferMemoryRequirements2<vk::MemoryRequirements2, vk::MemoryDedicatedRequirements>(
        vk::BufferMemoryRequirementsInfo2 {.buffer = *buffer});
    const auto dedicated = queried.get<vk::MemoryDedicatedRequirements>();
    return dedicated.requiresDedicatedAllocation == vk::True || dedicated.prefersDedicatedAllocation == vk::True;
}

void verifyPooledSharing(const panda::detail::MemoryPool& pool,
                         const vk::raii::Device& device,
                         const vk::raii::Buffer& firstBuffer,
                         const vk::raii::Buffer& secondBuffer,
                         const panda::detail::MemoryAllocation& first,
                         const panda::detail::MemoryAllocation& second)
{
    if (!requiresDedicated(device, firstBuffer) && !requiresDedicated(device, secondBuffer))
    {
        EXPECT_EQ(first.getMemory(), second.getMemory());
        EXPECT_EQ(pool.getStats().allocationCount, 1);
        EXPECT_LE(first.getRange().offset + first.getRange().size, second.getRange().offset);
    }
}

void verifyPoolBindings(panda::detail::MemoryPool& pool,
                        const vk::raii::Device& device,
                        vk::raii::Buffer& firstBuffer,
                        vk::raii::Buffer& secondBuffer,
                        std::uint32_t type)
{
    auto first = allocateForBuffer(pool, device, firstBuffer, type);
    auto second = allocateForBuffer(pool, device, secondBuffer, type);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    verifyPooledSharing(pool, device, firstBuffer, secondBuffer, *first, *second);
    const auto committed = pool.getStats().committedBytes;
    const auto bytes = first->getMappedBytes();
    ASSERT_TRUE(bytes);
    std::ranges::fill(*bytes, std::byte {0x2A});
    ASSERT_TRUE(first->flush(0, first->getRange().size));
    ASSERT_TRUE(first->invalidate(0, first->getRange().size));
    EXPECT_EQ(bytes->front(), std::byte {0x2A});
    firstBuffer.clear();
    secondBuffer.clear();
    first->reset();
    second->reset();
    EXPECT_EQ(pool.getStats().liveBytes, 0);
    EXPECT_LE(pool.getStats().allocationCount, 1);
    EXPECT_LE(pool.getStats().committedBytes, committed);
}
}

TEST_P(MemoryAllocationSystem, BindsTwoBuffersToOneMappedBlockWithoutValidationErrors)
{
    auto firstBuffer = makeTestBuffer(device);
    auto secondBuffer = makeTestBuffer(device);
    ASSERT_TRUE(firstBuffer);
    ASSERT_TRUE(secondBuffer);
    const auto request = vk::BufferMemoryRequirementsInfo2 {.buffer = **firstBuffer};
    const auto requirements =
        device.getBufferMemoryRequirements2<vk::MemoryRequirements2, vk::MemoryDedicatedRequirements>(request);
    const auto dedicated = requirements.get<vk::MemoryDedicatedRequirements>();
    if (dedicated.requiresDedicatedAllocation == vk::True || dedicated.prefersDedicatedAllocation == vk::True)
    {
        GTEST_SKIP() << "This buffer requires or prefers dedicated memory";
    }
    const auto required = requirements.get<vk::MemoryRequirements2>().memoryRequirements;
    const auto type = findHostMemory(required.memoryTypeBits);
    if (!type)
    {
        GTEST_SKIP() << "No host-visible buffer memory type with the requested coherence";
    }
    const auto atom = physical.getProperties().limits.nonCoherentAtomSize;
    auto block = panda::detail::MemoryBlock::create(physical,
                                                    device,
                                                    2 * (required.size + std::max(required.alignment, atom)),
                                                    *type);
    ASSERT_TRUE(block) << block.error().message;
    verifyBufferBindings(**block, *firstBuffer, *secondBuffer, required);
    std::cout << "Device: " << physical.getProperties().deviceName.data() << "; memory type: " << *type << '\n';
}

INSTANTIATE_TEST_SUITE_P(MemoryTypes,
                         MemoryAllocationSystem,
                         testing::Bool(),
                         [](const testing::TestParamInfo<bool>& parameterInfo) {
                             return parameterInfo.param ? std::string {"Coherent"} : std::string {"Noncoherent"};
                         });

TEST_P(MemoryAllocationSystem, PoolSharesMemoryAndReleasesRangesWithValidation)
{
    auto firstBuffer = makeTestBuffer(device);
    auto secondBuffer = makeTestBuffer(device);
    ASSERT_TRUE(firstBuffer);
    ASSERT_TRUE(secondBuffer);
    const auto requirements = firstBuffer->getMemoryRequirements();
    const auto type = findHostMemory(requirements.memoryTypeBits);
    if (!type)
    {
        GTEST_SKIP() << "No host-visible buffer memory type with the requested coherence";
    }
    auto pool = panda::detail::MemoryPool::create(physical, device);
    EXPECT_EQ(pool->getStats().allocationCount, 0);
    verifyPoolBindings(*pool, device, *firstBuffer, *secondBuffer, *type);
    std::cout << "Pool device: " << physical.getProperties().deviceName.data() << "; memory type: " << *type << '\n';
}

TEST_P(MemoryAllocationSystem, DedicatedMemoryHasZeroOffsetAndIsFreedOnReset)
{
    auto buffer = makeTestBuffer(device);
    ASSERT_TRUE(buffer);
    const auto requirements = buffer->getMemoryRequirements();
    const auto type = findHostMemory(requirements.memoryTypeBits);
    if (!type)
    {
        GTEST_SKIP() << "No host-visible buffer memory type with the requested coherence";
    }
    auto pool = panda::detail::MemoryPool::create(physical, device);
    auto allocation = allocateForBuffer(*pool, device, *buffer, *type, true);
    ASSERT_TRUE(allocation);
    EXPECT_EQ(allocation->getRange().offset, 0);
    EXPECT_EQ(pool->getStats().allocationCount, 1);
    EXPECT_EQ(pool->getStats().committedBytes, requirements.size);
    ASSERT_TRUE(allocation->flush(0, requirements.size));
    buffer->clear();
    allocation->reset();
    EXPECT_EQ(pool->getStats().allocationCount, 0);
    EXPECT_EQ(pool->getStats().committedBytes, 0);
}
