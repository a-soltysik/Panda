// clang-format off
#include "VulkanHpp.hpp" // IWYU pragma: keep
#include <vulkan/vulkan.hpp>
// clang-format on

#include "presentation/FrameExecution.hpp"

#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <new>
#include <numeric>
#include <panda/CompletionPoint.hpp>
#include <panda/Error.hpp>
#include <panda/RenderView.hpp>
#include <panda/Texture.hpp>
#include <utility>
#include <vector>

#include "performance/PerformanceManagerImpl.hpp"
#include "renderer/MaterialTextureSlots.hpp"
#include "renderer/MeshRenderer.hpp"

namespace panda::core::vulkan::presentation
{
namespace
{
void retainTexture(std::vector<Texture>& retained, const Texture& texture)
{
    if (texture.isValid())
    {
        retained.push_back(texture);
    }
}

void retainMaterialTextures(std::vector<Texture>& retained, const RenderView& view)
{
    retained.reserve(view.meshes.size() * renderer::materialTextureSlots.size());
    for (const auto& draw : view.meshes)
    {
        for (const auto& slot : renderer::materialTextureSlots)
        {
            retainTexture(retained, draw.material.*(slot.texture));
        }
    }
}

auto createFrameCommands(const vk::raii::Device& device,
                         std::uint32_t queueFamily,
                         vk::raii::CommandPool& pool,
                         vk::CommandBuffer& command) -> std::expected<void, Error>
{
    const auto poolInfo = VkCommandPoolCreateInfo {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                                   .pNext = nullptr,
                                                   .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                                                   .queueFamilyIndex = queueFamily};
    VkCommandPool rawPool {};
    const auto createdPool =
        device.getDispatcher()->vkCreateCommandPool(static_cast<VkDevice>(*device), &poolInfo, nullptr, &rawPool);
    if (createdPool != VK_SUCCESS)
    {
        return std::unexpected {makeVulkanError(static_cast<vk::Result>(createdPool))};
    }
    pool = vk::raii::CommandPool {device, rawPool};
    const auto allocateInfo = vk::CommandBufferAllocateInfo {.commandPool = *pool,
                                                             .level = vk::CommandBufferLevel::ePrimary,
                                                             .commandBufferCount = 1};
    auto commands = (*device).allocateCommandBuffers(allocateInfo, *device.getDispatcher());
    if (commands.result != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(commands.result)};
    }
    command = commands.value.front();
    return {};
}

void recordClearRendering(vk::CommandBuffer command, const FrameOutput& output)
{
    const auto attachment = vk::RenderingAttachmentInfo {
        .imageView = output.imageView,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = vk::ClearValue {.color = renderer::makeClearColor(output.colorFormat)}};
    const auto rendering = vk::RenderingInfo {.renderArea = vk::Rect2D {.extent = output.extent},
                                              .layerCount = 1,
                                              .colorAttachmentCount = 1,
                                              .pColorAttachments = &attachment};
    command.beginRendering(rendering);
    command.endRendering();
}
}

FrameExecution::FrameExecution(const FrameExecutionCreateInfo& info)
    : _physicalDevice {info.physicalDevice},
      _device {info.device},
      _queueFamily {info.queueFamily},
      _submissions {info.submissions},
      _pool {info.pool},
      _performance {info.performance},
      _maxLights {info.maxLights}
{
}

FrameExecution::~FrameExecution() = default;

auto FrameExecution::create(const FrameExecutionCreateInfo& info)
    -> std::expected<std::unique_ptr<FrameExecution>, Error>
{
    auto result = std::unique_ptr<FrameExecution> {new FrameExecution {info}};
    if (const auto initialized = result->initialize(); !initialized)
    {
        return std::unexpected {initialized.error()};
    }
    return result;
}

auto FrameExecution::initialize() -> std::expected<void, Error>
{
    for (auto& slot : _slots)
    {
        if (const auto commands = createFrameCommands(_device, _queueFamily, slot.pool, slot.command); !commands)
        {
            return std::unexpected {commands.error()};
        }
    }
    return {};
}

auto FrameExecution::retainViewResources(ExecutionSlot& slot, const RenderView& view) -> std::expected<void, Error>
{
    slot.retainedMeshes.clear();
    slot.retainedTextures.clear();
    try
    {
        slot.retainedMeshes.reserve(view.meshes.size());
        for (const auto& draw : view.meshes)
        {
            slot.retainedMeshes.push_back(draw.mesh);
        }
        retainMaterialTextures(slot.retainedTextures, view);
    }
    catch (const std::bad_alloc&)
    {
        slot.retainedMeshes.clear();
        slot.retainedTextures.clear();
        return std::unexpected {makeError(ErrorCode::ResourceExhausted, "Unable to retain render view resources")};
    }
    return {};
}

auto FrameExecution::prepareSlot(std::size_t index, const RenderView& view) -> std::expected<void, Error>
{
    auto& slot = _slots.at(index);
    if (const auto waited = waitCompletion(slot.lastCompletion); !waited)
    {
        return std::unexpected {waited.error()};
    }
    return retainViewResources(slot, view);
}

auto FrameExecution::beginCommands(std::size_t index) -> std::expected<void, Error>
{
    auto& slot = _slots.at(index);
    const auto reset = slot.pool.reset();
    if (reset != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(reset)};
    }
    const auto begun = slot.command.begin(vk::CommandBufferBeginInfo {});
    if (begun != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(begun)};
    }
    return {};
}

auto FrameExecution::recordContent(std::size_t index, const FrameOutput& output, const RenderView& view)
    -> std::expected<void, Error>
{
    if (_performance != nullptr)
    {
        if (const auto timestamp = _performance->recordStart(index, _slots.at(index).command); !timestamp)
        {
            return std::unexpected {timestamp.error()};
        }
    }
    if (const auto content = recordViewContent(index, output, view); !content)
    {
        return std::unexpected {content.error()};
    }
    if (_performance != nullptr)
    {
        _performance->recordEnd(index, _slots.at(index).command);
    }
    return {};
}

auto FrameExecution::recordViewContent(std::size_t index, const FrameOutput& output, const RenderView& view)
    -> std::expected<void, Error>
{
    if (view.meshes.empty())
    {
        recordClearRendering(_slots.at(index).command, output);
        return {};
    }
    return recordMeshDraws(_slots.at(index), index, output, view);
}

auto FrameExecution::recordMeshDraws(ExecutionSlot& slot,
                                     std::size_t index,
                                     const FrameOutput& output,
                                     const RenderView& view) -> std::expected<void, Error>
{
    if (const auto ready = createOrResizeMeshRenderer(output); !ready)
    {
        return std::unexpected {ready.error()};
    }
    if (const auto updated = _renderer->updateFrame(index, view); !updated)
    {
        return std::unexpected {updated.error()};
    }
    _renderer->record(slot.command, output.imageView, index, view);
    return {};
}

auto FrameExecution::createOrResizeMeshRenderer(const FrameOutput& output) -> std::expected<void, Error>
{
    if (_pool == nullptr)
    {
        return std::unexpected {makeError(ErrorCode::BackendFailure, "Mesh rendering requires a Context memory pool")};
    }
    if (!_renderer)
    {
        auto renderer = renderer::MeshRenderer::create({.physicalDevice = _physicalDevice,
                                                        .device = _device,
                                                        .pool = *_pool,
                                                        .extent = output.extent,
                                                        .colorFormat = output.colorFormat,
                                                        .frameCount = _slots.size(),
                                                        .maxLights = _maxLights});
        if (!renderer)
        {
            return std::unexpected {renderer.error()};
        }
        _renderer = std::move(*renderer);
        return {};
    }
    return _renderer->resize(output.extent, output.colorFormat);
}

auto FrameExecution::endCommands(std::size_t index) -> std::expected<void, Error>
{
    const auto ended = _slots.at(index).command.end();
    if (ended != vk::Result::eSuccess)
    {
        return std::unexpected {makeVulkanError(ended)};
    }
    return {};
}

auto FrameExecution::command(std::size_t index) const -> vk::CommandBuffer
{
    return _slots.at(index).command;
}

void FrameExecution::setLastCompletion(std::size_t index, std::uint64_t value)
{
    _slots.at(index).lastCompletion = value;
}

void FrameExecution::markSubmitted(std::size_t index, std::uint64_t value, bool hadMeshDraws)
{
    setLastCompletion(index, value);
    if (_renderer != nullptr && hadMeshDraws)
    {
        _renderer->markSubmitted(index);
    }
    if (_performance != nullptr)
    {
        _performance->markSubmitted(index, _submissions.getPoint(value));
    }
}

void FrameExecution::discardUnsubmittedInputs(std::size_t index)
{
    auto& slot = _slots.at(index);
    slot.retainedMeshes.clear();
    slot.retainedTextures.clear();
}

auto FrameExecution::waitCompletion(std::uint64_t value) const -> std::expected<void, Error>
{
    static constexpr auto timeoutNanoseconds = std::uint64_t {10'000'000'000};
    return _submissions.waitValue(value, timeoutNanoseconds).and_then([](WaitStatus status) -> Result<void> {
        if (status == WaitStatus::Timeout)
        {
            return std::unexpected {makeError(ErrorCode::Timeout, "GPU completion wait timed out")};
        }
        return {};
    });
}

auto FrameExecution::drain() -> std::expected<void, Error>
{
    const auto last = std::accumulate(_slots.begin(),
                                      _slots.end(),
                                      std::uint64_t {0},
                                      [](std::uint64_t completion, const ExecutionSlot& slot) {
                                          return std::max(completion, slot.lastCompletion);
                                      });
    if (const auto waited = waitCompletion(last); !waited)
    {
        return std::unexpected {waited.error()};
    }
    for (std::size_t index = 0; index < _slots.size(); ++index)
    {
        discardUnsubmittedInputs(index);
    }
    return {};
}

auto FrameExecution::isGpuTimestampSamplingEnabled() const noexcept -> bool
{
    return _performance != nullptr && _performance->getCapabilities().isGpuTimestampSamplingEnabled;
}

auto FrameExecution::getSampleCount() const noexcept -> vk::SampleCountFlagBits
{
    return _renderer != nullptr ? _renderer->getSampleCount() : vk::SampleCountFlagBits::e1;
}
}
