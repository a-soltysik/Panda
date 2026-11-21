// clang-format off
#include <panda/base/Error.hpp>
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
#include <iterator>
#include <memory>
#include <numeric>
#include <panda/rendering/RenderView.hpp>
#include <panda/rendering/SampleCount.hpp>
#include <panda/resources/Texture.hpp>
#include <panda/submission/CompletionPoint.hpp>
#include <panda/vulkan/Error.hpp>
#include <utility>
#include <vector>

#include "FrameResultMapping.hpp"
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

void retainViewTextures(std::vector<Texture>& retained, const RenderView& view)
{
    retained.reserve((view.meshes.size() * renderer::materialTextureSlots.size()) + (view.environment ? 2U : 0U) +
                     (view.skybox ? 1U : 0U));
    for (const auto& draw : view.meshes)
    {
        for (const auto& slot : renderer::materialTextureSlots)
        {
            retainTexture(retained, draw.material.*(slot.texture));
        }
    }
    if (view.environment)
    {
        retainTexture(retained, view.environment->diffuseIrradiance);
        retainTexture(retained, view.environment->specularRadiance);
    }
    if (view.skybox)
    {
        retainTexture(retained, *view.skybox);
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
      _textureFilteringPreset {info.textureFilteringPreset},
      _maxLights {info.maxLights},
      _performanceSlotBase {info.performanceSlotBase}
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

auto FrameExecution::retainViewResources(ExecutionSlot& slot,
                                         const RenderView& view,
                                         std::shared_ptr<::panda::detail::OffscreenTargetResource> target)
    -> std::expected<void, Error>
{
    slot.retainedMeshes.clear();
    slot.retainedMeshes.reserve(view.meshes.size());
    std::ranges::transform(view.meshes, std::back_inserter(slot.retainedMeshes), &MeshDraw::mesh);
    slot.retainedTextures.clear();
    retainViewTextures(slot.retainedTextures, view);
    slot.retainedTarget = std::move(target);
    return {};
}

auto FrameExecution::prepareSlot(std::size_t index,
                                 const RenderView& view,
                                 std::shared_ptr<::panda::detail::OffscreenTargetResource> target)
    -> std::expected<void, Error>
{
    auto& slot = _slots.at(index);
    if (const auto waited = waitCompletion(slot.lastCompletion); !waited)
    {
        return std::unexpected {waited.error()};
    }
    return retainViewResources(slot, view, std::move(target));
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
        if (const auto timestamp = _performance->recordStart(_performanceSlotBase + index, _slots.at(index).command);
            !timestamp)
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
        _performance->recordEnd(_performanceSlotBase + index, _slots.at(index).command);
    }
    return {};
}

auto FrameExecution::recordViewContent(std::size_t index, const FrameOutput& output, const RenderView& view)
    -> std::expected<void, Error>
{
    if (view.meshes.empty() && !view.skybox)
    {
        recordClearRendering(_slots.at(index).command, output);
        return {};
    }
    return recordRendererView(_slots.at(index), index, output, view);
}

auto FrameExecution::recordRendererView(ExecutionSlot& slot,
                                        std::size_t index,
                                        const FrameOutput& output,
                                        const RenderView& view) -> std::expected<void, Error>
{
    if (const auto updated = _renderer->updateFrame(index, view); !updated)
    {
        return std::unexpected {updated.error()};
    }
    _renderer->record(slot.command, output.imageView, index, view);
    return {};
}

auto FrameExecution::prepareOutput(vk::Extent2D extent, vk::Format colorFormat, const RenderView& view) -> Result<void>
{
    if (view.meshes.empty() && !view.skybox)
    {
        return {};
    }
    const auto requestedSampleCount = view.meshes.empty() ? SampleCount::One : view.requestedSampleCount;
    if (!_renderer)
    {
        return initializeRenderer(extent, colorFormat, requestedSampleCount);
    }
    const auto& current = _renderer->getOutputDescription();
    if (current.extent == extent && current.colorFormat == colorFormat &&
        current.requestedSampleCount == requestedSampleCount)
    {
        return {};
    }
    // The attachment and pipeline replacements may release objects used by either slot.
    return drain().and_then([&] {
        return _renderer->resize(extent, colorFormat, requestedSampleCount);
    });
}

auto FrameExecution::initializeRenderer(vk::Extent2D extent, vk::Format colorFormat, SampleCount requested)
    -> Result<void>
{
    if (_pool == nullptr)
    {
        return std::unexpected {makeError(ErrorCode::BackendFailure, "Mesh rendering requires an Engine memory pool")};
    }
    return renderer::MeshRenderer::create({.physicalDevice = _physicalDevice,
                                           .device = _device,
                                           .pool = *_pool,
                                           .extent = extent,
                                           .colorFormat = colorFormat,
                                           .frameCount = _slots.size(),
                                           .maxLights = _maxLights,
                                           .textureFilteringPreset = _textureFilteringPreset,
                                           .requestedSampleCount = requested})
        .transform([this](std::unique_ptr<renderer::MeshRenderer> meshRenderer) {
            _renderer = std::move(meshRenderer);
        });
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

void FrameExecution::markSubmitted(std::size_t index, std::uint64_t value, bool hadRendererWork)
{
    setLastCompletion(index, value);
    if (_renderer != nullptr && hadRendererWork)
    {
        _renderer->markSubmitted(index);
    }
    if (_performance != nullptr)
    {
        _performance->markSubmitted(_performanceSlotBase + index, _submissions.getPoint(value));
    }
}

void FrameExecution::discardUnsubmittedInputs(std::size_t index)
{
    auto& slot = _slots.at(index);
    slot.retainedMeshes.clear();
    slot.retainedTextures.clear();
    slot.retainedTarget.reset();
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
    return _performance != nullptr;
}

auto FrameExecution::getSampleCount(const RenderView& view) const noexcept -> SampleCount
{
    return view.meshes.empty() ? SampleCount::One : mapSampleCount(_renderer->getSampleCount());
}

auto FrameExecution::getEffectiveAnisotropy(const RenderView& view) const noexcept -> float
{
    return view.meshes.empty() || !_textureFilteringPreset
               ? 1.0F
               : _renderer->getEffectiveAnisotropy(*_textureFilteringPreset);
}
}
