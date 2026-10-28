// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include "external/ExternalContext.hpp"

#include <algorithm>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <optional>
#include <panda/Buffer.hpp>
#include <panda/CompletionPoint.hpp>
#include <panda/Error.hpp>
#include <panda/ExternalBuffer.hpp>
#include <panda/ExternalSemaphoreHandle.hpp>
#include <string_view>
#include <utility>
#include <vector>

#include "SubmissionQueue.hpp"
#include "VulkanError.hpp"
#include "external/ExternalBufferOwnershipTransferCommand.hpp"
#include "external/ExternalSemaphoreResource.hpp"
#include "panda/ExternalMemoryHandle.hpp"
#include "resources/BufferResource.hpp"
#include "upload/UploadManager.hpp"

namespace panda::detail
{
namespace
{
auto hasExtension(const std::vector<vk::ExtensionProperties>& extensions, std::string_view name) -> bool
{
    return std::ranges::contains(extensions, name, [](const auto& extension) {
        return std::string_view {extension.extensionName.data()};
    });
}

auto deviceExtensions(const vk::raii::PhysicalDevice& physicalDevice) -> Result<std::vector<vk::ExtensionProperties>>
{
    const auto native = *physicalDevice;
    return core::vulkan::detail::enumerateVulkan<vk::ExtensionProperties>(
        [&](std::uint32_t* count, vk::ExtensionProperties* output) {
            return native.enumerateDeviceExtensionProperties(nullptr, count, output);
        });
}

auto unavailableExternalSupport(bool required) -> Result<std::optional<ExternalSupport>>
{
    if (required)
    {
        return std::unexpected {
            makeError(ErrorCode::Unsupported, "The selected Vulkan device cannot export external memory handles")};
    }
    return std::nullopt;
}

auto hasExportableTimelineSemaphore(const vk::raii::PhysicalDevice& physicalDevice,
                                    const std::vector<vk::ExtensionProperties>& listed,
                                    std::string_view extension,
                                    vk::ExternalSemaphoreHandleTypeFlagBits handleType) -> bool
{
    if (!hasExtension(listed, extension))
    {
        return false;
    }
    const auto timelineInfo = vk::SemaphoreTypeCreateInfo {.semaphoreType = vk::SemaphoreType::eTimeline};
    const auto info = vk::PhysicalDeviceExternalSemaphoreInfo {.pNext = &timelineInfo, .handleType = handleType};
    const auto properties = physicalDevice.getExternalSemaphoreProperties(info);
    return (properties.externalSemaphoreFeatures & vk::ExternalSemaphoreFeatureFlagBits::eExportable) &&
           (properties.compatibleHandleTypes & handleType);
}

auto queryExternalSupport(const vk::raii::PhysicalDevice& physicalDevice, bool required, ExternalSupport support)
    -> Result<std::optional<ExternalSupport>>
{
    const auto listed = deviceExtensions(physicalDevice);
    if (!listed)
    {
        return std::unexpected {listed.error()};
    }
    if (!hasExtension(*listed, support.deviceExtensions.front()))
    {
        return unavailableExternalSupport(required);
    }
#ifdef _WIN32
    static constexpr auto semaphoreExtension = std::string_view {"VK_KHR_external_semaphore_win32"};
    static constexpr auto semaphoreHandleType = vk::ExternalSemaphoreHandleTypeFlagBits::eOpaqueWin32;
    static constexpr auto publicSemaphoreHandleType = ExternalSemaphoreHandleType::OpaqueWin32;
#elifdef __linux__
    static constexpr auto semaphoreExtension = std::string_view {"VK_KHR_external_semaphore_fd"};
    static constexpr auto semaphoreHandleType = vk::ExternalSemaphoreHandleTypeFlagBits::eOpaqueFd;
    static constexpr auto publicSemaphoreHandleType = ExternalSemaphoreHandleType::OpaqueFd;
#endif
#if defined(_WIN32) || defined(__linux__)
    if (hasExportableTimelineSemaphore(physicalDevice, *listed, semaphoreExtension, semaphoreHandleType))
    {
        support.deviceExtensions.emplace_back(semaphoreExtension);
        support.semaphoreHandleType = std::pair {semaphoreHandleType, publicSemaphoreHandleType};
    }
#endif
    return std::optional {std::move(support)};
}
}

auto ExternalContext::querySupport(const vk::raii::PhysicalDevice& physicalDevice, bool required)
    -> Result<std::optional<ExternalSupport>>
{
#ifdef _WIN32
    return queryExternalSupport(physicalDevice,
                                required,
                                ExternalSupport {.deviceExtensions = {"VK_KHR_external_memory_win32"},
                                                 .memoryHandleType = vk::ExternalMemoryHandleTypeFlagBits::eOpaqueWin32,
                                                 .publicHandleType = ExternalMemoryHandleType::OpaqueWin32,
                                                 .semaphoreHandleType = std::nullopt});
#elifdef __linux__
    return queryExternalSupport(physicalDevice,
                                required,
                                ExternalSupport {.deviceExtensions = {"VK_KHR_external_memory_fd"},
                                                 .memoryHandleType = vk::ExternalMemoryHandleTypeFlagBits::eOpaqueFd,
                                                 .publicHandleType = ExternalMemoryHandleType::OpaqueFd,
                                                 .semaphoreHandleType = std::nullopt});
#else
    std::ignore = physicalDevice;
    return unavailableExternalSupport(required);
#endif
}

ExternalContext::ExternalContext(CreateInfo info)
    : _physicalDevice {info.physicalDevice},
      _device {info.device},
      _pool {info.pool},
      _submissions {info.submissions},
      _uploads {info.uploads},
      _queueFamily {info.queueFamily},
      _support {std::move(info.support)}
{
    const auto properties =
        _physicalDevice.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceIDProperties>();
    const auto& uuid = properties.get<vk::PhysicalDeviceIDProperties>().deviceUUID;
    std::ranges::copy(uuid, _deviceUuid.begin());
}

ExternalContext::~ExternalContext() = default;

auto ExternalContext::supportsSemaphoreExport() const noexcept -> bool
{
    return _support.semaphoreHandleType.has_value();
}

auto ExternalContext::createBuffer(const BufferDesc& description) -> Result<ExternalBuffer>
{
    auto externalBuffer = createBufferWithMemory(description);
    if (!externalBuffer)
    {
        return std::unexpected {std::move(externalBuffer.error())};
    }
    auto semaphore = createSemaphoreResource();
    if (!semaphore)
    {
        return std::unexpected {std::move(semaphore.error())};
    }
    externalBuffer->_semaphoreHandle = std::move(semaphore->first);
    BufferResource::getResource(externalBuffer->_buffer)->setExternalInterop(*this, std::move(semaphore->second));
    return std::move(*externalBuffer);
}

auto ExternalContext::getHandoffBuffer(const Buffer& buffer) const -> Result<HandoffBuffer>
{
    if (!buffer.isValid())
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "External buffer is empty")};
    }
    const auto resource = BufferResource::getResource(buffer);
    const auto externalContext = resource->getExternalContext();
    if (!externalContext || &externalContext->get() != this)
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "Buffer is not an external buffer from this Context")};
    }
    const auto semaphore = resource->getExternalSemaphore();
    if (!semaphore || !_submissions)
    {
        return std::unexpected {
            makeError(ErrorCode::Unsupported, "External timeline-semaphore handoff is unavailable")};
    }
    return HandoffBuffer {.owner = buffer,
                          .resource = resource,
                          .semaphore = semaphore,
                          .submissions = _submissions->get()};
}

auto ExternalContext::submitHandoff(const HandoffBuffer& buffer,
                                    ExternalBufferOwnershipTransferDirection direction,
                                    std::uint64_t semaphoreValue) -> Result<void>
{
    if (const auto prepared = prepareHandoff(); !prepared)
    {
        return std::unexpected {prepared.error()};
    }
    if (const auto collected = collectCompleted(); !collected)
    {
        return std::unexpected {collected.error()};
    }
    auto command = ExternalBufferOwnershipTransferCommand::create(_device, buffer.owner, _queueFamily, direction);
    if (!command)
    {
        return std::unexpected {command.error()};
    }
    _pending.emplace_back(0, std::move(*command));
    const auto submitted = direction == ExternalBufferOwnershipTransferDirection::Release
                               ? buffer.submissions.get().submitExternalSignal(_pending.back().second->getCommand(),
                                                                               buffer.semaphore->getNative(),
                                                                               semaphoreValue)
                               : buffer.submissions.get().submitExternalWait(_pending.back().second->getCommand(),
                                                                             buffer.semaphore->getNative(),
                                                                             semaphoreValue);
    if (!submitted)
    {
        _pending.pop_back();
        return std::unexpected {submitted.error()};
    }
    _pending.back().first = *submitted;
    return {};
}

auto ExternalContext::release(const Buffer& buffer) -> Result<std::uint64_t>
{
    auto handoff = getHandoffBuffer(buffer);
    if (!handoff)
    {
        return std::unexpected {handoff.error()};
    }
    auto& resource = *handoff->resource;
    if (resource.isExternallyOwned())
    {
        return std::unexpected {makeError(ErrorCode::InvalidArgument, "External buffer is already released")};
    }
    const auto previousValue = resource.getExternalTimelineValue();
    if (previousValue >= std::numeric_limits<std::uint64_t>::max() - 1)
    {
        return std::unexpected {makeError(ErrorCode::ResourceExhausted, "External timeline values are exhausted")};
    }
    const auto signalValue = previousValue + 1;
    if (const auto submitted = submitHandoff(*handoff, ExternalBufferOwnershipTransferDirection::Release, signalValue);
        !submitted)
    {
        return std::unexpected {submitted.error()};
    }
    resource.setExternalTimelineValue(signalValue);
    resource.setExternallyOwned(true);
    return signalValue;
}

auto ExternalContext::acquire(const Buffer& buffer, std::uint64_t externalSignalValue) -> Result<void>
{
    auto handoff = getHandoffBuffer(buffer);
    if (!handoff)
    {
        return std::unexpected {handoff.error()};
    }
    auto& resource = *handoff->resource;
    const auto previousValue = resource.getExternalTimelineValue();
    if (!resource.isExternallyOwned() || previousValue == std::numeric_limits<std::uint64_t>::max() ||
        externalSignalValue != previousValue + 1)
    {
        return std::unexpected {
            makeError(ErrorCode::InvalidArgument, "External acquire uses an unexpected timeline value")};
    }
    if (const auto submitted =
            submitHandoff(*handoff, ExternalBufferOwnershipTransferDirection::Acquire, externalSignalValue);
        !submitted)
    {
        return std::unexpected {submitted.error()};
    }
    resource.setExternalTimelineValue(externalSignalValue);
    resource.setExternallyOwned(false);
    return {};
}

auto ExternalContext::prepareHandoff() -> Result<void>
{
    if (!_uploads)
    {
        return {};
    }
    if (_uploads->hasActiveBatches())
    {
        return std::unexpected {
            makeError(ErrorCode::ResourcesInUse, "Destroy or submit upload batches before handoff")};
    }
    return _uploads->collect();
}

auto ExternalContext::collectCompleted() -> Result<void>
{
    if (_pending.empty())
    {
        return {};
    }
    if (!_submissions)
    {
        return std::unexpected {makeError(ErrorCode::BackendFailure, "Pending external handoff has no queue")};
    }
    return collectPending(_submissions->get());
}

auto ExternalContext::collectPending(SubmissionQueue& submissions) -> Result<void>
{
    while (!_pending.empty())
    {
        const auto completed = submissions.poll(submissions.getPoint(_pending.front().first));
        if (!completed)
        {
            return std::unexpected {completed.error()};
        }
        if (!*completed)
        {
            break;
        }
        _pending.pop_front();
    }
    return {};
}

auto ExternalContext::drain(std::uint64_t timeoutNanoseconds) -> Result<void>
{
    if (_pending.empty())
    {
        return {};
    }
    if (!_submissions)
    {
        return std::unexpected {makeError(ErrorCode::BackendFailure, "Pending external handoff has no queue")};
    }
    const auto completed = _submissions->get().waitValue(_pending.back().first, timeoutNanoseconds);
    if (!completed)
    {
        return std::unexpected {completed.error()};
    }
    if (*completed == WaitStatus::Timeout)
    {
        return std::unexpected {makeError(ErrorCode::Timeout, "External handoff completion wait timed out")};
    }
    _pending.clear();
    return {};
}

auto ExternalContext::createBufferWithMemory(const BufferDesc& description) -> Result<ExternalBuffer>
{
    auto resource = BufferResource::createExportable(_device, _pool, description, _support.memoryHandleType);
    if (!resource)
    {
        return std::unexpected {std::move(resource.error())};
    }
    const auto allocationSize = (*resource)->getReservedSize();
    const auto handle = exportMemoryHandle((*resource)->getNativeMemory());
    if (!handle)
    {
        return std::unexpected {handle.error()};
    }
    auto memoryHandle = ExternalMemoryHandle {*handle, _support.publicHandleType};
    auto externalBuffer = ExternalBuffer {};
    externalBuffer._buffer = Buffer {std::move(*resource)};
    externalBuffer._handle = std::move(memoryHandle);
    externalBuffer._allocationSize = allocationSize;
    externalBuffer._deviceUuid = _deviceUuid;
    return externalBuffer;
}

auto ExternalContext::createSemaphoreResource()
    -> Result<std::pair<ExternalSemaphoreHandle, std::shared_ptr<ExternalSemaphoreResource>>>
{
    const auto handleTypes = _support.semaphoreHandleType;
    if (!handleTypes)
    {
        return std::pair {ExternalSemaphoreHandle {}, std::shared_ptr<ExternalSemaphoreResource> {}};
    }
    const auto [nativeHandleType, publicHandleType] = *handleTypes;
    const auto exportInfo = vk::ExportSemaphoreCreateInfo {.handleTypes = nativeHandleType};
    const auto timelineInfo =
        vk::SemaphoreTypeCreateInfo {.pNext = &exportInfo, .semaphoreType = vk::SemaphoreType::eTimeline};
    const auto createInfo = vk::SemaphoreCreateInfo {.pNext = &timelineInfo};
    auto nativeSemaphore = vk::Semaphore {};
    const auto created = (*_device).createSemaphore(&createInfo, nullptr, &nativeSemaphore, *_device.getDispatcher());
    if (created != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(created)};
    }
    auto semaphoreState = std::make_shared<ExternalSemaphoreResource>(vk::raii::Semaphore {_device, nativeSemaphore});
    const auto exported = exportSemaphoreHandle(nativeSemaphore, nativeHandleType);
    if (!exported)
    {
        return std::unexpected {exported.error()};
    }
    return std::pair {
        ExternalSemaphoreHandle {*exported, publicHandleType},
        std::move(semaphoreState)
    };
}

auto ExternalContext::exportMemoryHandle(vk::DeviceMemory memory) const -> Result<std::intptr_t>
{
#ifdef _WIN32
    const auto info = vk::MemoryGetWin32HandleInfoKHR {.memory = memory, .handleType = _support.memoryHandleType};
    const auto exported = _device.getMemoryWin32HandleKHR(info);
#elifdef __linux__
    const auto info = vk::MemoryGetFdInfoKHR {.memory = memory, .handleType = _support.memoryHandleType};
    const auto exported = _device.getMemoryFdKHR(info);
#else
    std::ignore = memory;
    return std::unexpected {makeError(ErrorCode::Unsupported, "External memory handles are unsupported on this OS")};
#endif
#ifdef _WIN32
    if (exported.result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(exported.result)};
    }
    // Vulkan returns a HANDLE here; Panda carries opaque native handles as intptr_t.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast, performance-no-int-to-ptr)
    return reinterpret_cast<std::intptr_t>(exported.value);
#elifdef __linux__
    return static_cast<std::intptr_t>(exported.value);
#endif
}

auto ExternalContext::exportSemaphoreHandle(vk::Semaphore semaphore,
                                            vk::ExternalSemaphoreHandleTypeFlagBits handleType) const
    -> Result<std::intptr_t>
{
#ifdef _WIN32
    const auto info = vk::SemaphoreGetWin32HandleInfoKHR {.semaphore = semaphore, .handleType = handleType};
    const auto exported = _device.getSemaphoreWin32HandleKHR(info);
#elifdef __linux__
    const auto info = vk::SemaphoreGetFdInfoKHR {.semaphore = semaphore, .handleType = handleType};
    const auto exported = _device.getSemaphoreFdKHR(info);
#else
    std::ignore = semaphore;
    std::ignore = handleType;
    return std::unexpected {makeError(ErrorCode::Unsupported, "External semaphore handles are unsupported on this OS")};
#endif
#ifdef _WIN32
    if (exported.result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(exported.result)};
    }
    // Vulkan returns a HANDLE here; Panda carries opaque native handles as intptr_t.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast, performance-no-int-to-ptr)
    return reinterpret_cast<std::intptr_t>(exported.value);
#elifdef __linux__
    if (exported.result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(exported.result)};
    }
    return static_cast<std::intptr_t>(exported.value);
#endif
}
}
