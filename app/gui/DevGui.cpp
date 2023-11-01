#include "DevGui.h"

#include <fmt/ranges.h>
#include <imgui.h>
#include <implot.h>

namespace app
{

DevGui::DevGui(const panda::Window& window)
    : _window {window}
{
}

auto DevGui::render() -> void
{
    const auto windowSize =
        ImVec2 {static_cast<float>(_window.getSize().x) / 3.f, static_cast<float>(_window.getSize().y)};
    ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Once);
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_Once);
    ImGui::SetNextWindowCollapsed(true, ImGuiCond_Once);

    ImGui::Begin("Profiler", nullptr, static_cast<uint32_t>(ImGuiWindowFlags_NoScrollbar));

    const auto plotSize = ImVec2 {ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y / 3};

    ImPlot::PushStyleVar(ImPlotStyleVar_PlotDefaultSize, plotSize);
    if (ImPlot::BeginPlot("Framerate"))
    {
        ImPlot::SetupLegend(ImPlotLocation_NorthEast);
        ImPlot::SetupAxes("Time [s]", "Framerate [FPS]", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_None);
        ImPlot::SetupAxesLimits(0,
                                static_cast<double>(_frameRates.size()),
                                0,
                                static_cast<double>(*std::ranges::max_element(_frameRates)),
                                ImPlotCond_Always);
        ImPlot::PlotLine("", _frameRates.data(), static_cast<int>(_frameRates.size()));
        ImPlot::EndPlot();
    }

    if (ImPlot::BeginPlot("Physical memory usage"))
    {
        ImPlot::SetupLegend(ImPlotLocation_NorthEast);
        ImPlot::SetupAxes("Time [s]", "Memory usage [MB]", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_None);
        ImPlot::SetupAxesLimits(0,
                                static_cast<double>(_physicalMemoryUsages.size()),
                                0,
                                static_cast<double>(*std::ranges::max_element(_physicalMemoryUsages)),
                                ImPlotCond_Always);
        ImPlot::PlotLine("", _physicalMemoryUsages.data(), static_cast<int>(_physicalMemoryUsages.size()));
        ImPlot::EndPlot();
    }

    if (ImPlot::BeginPlot("CPU usage"))
    {
        static constexpr auto percent100 = 100.;
        ImPlot::SetupLegend(ImPlotLocation_NorthEast);
        ImPlot::SetupAxes("Time [s]", "CPU usage [%]", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_None);
        ImPlot::SetupAxesLimits(0, static_cast<double>(_cpuUsages.size()), 0, percent100, ImPlotCond_Always);
        ImPlot::PlotLine("", _cpuUsages.data(), static_cast<int>(_cpuUsages.size()));
        ImPlot::EndPlot();
    }

    _time += ImGui::GetIO().DeltaTime;

    static constexpr auto second = 1.f;
    if (_time > second)
    {
        _time = 0.f;

        std::ranges::rotate(_physicalMemoryUsages, std::ranges::prev(std::ranges::end(_physicalMemoryUsages)));
        std::ranges::rotate(_frameRates, std::ranges::prev(std::ranges::end(_frameRates)));
        std::ranges::rotate(_cpuUsages, std::ranges::prev(std::ranges::end(_cpuUsages)));

        const auto physicalMemoryUsage =
            backend::getProfiler().getPhysicalMemoryUsage() / (size_t {1024} * size_t {1024});

        _physicalMemoryUsages.front() = physicalMemoryUsage;
        _frameRates.front() = ImGui::GetIO().Framerate;
        _cpuUsages.front() = static_cast<float>(backend::getProfiler().getCpuUsageInPercents());

        const auto totalPhysicalMemory =
            backend::getProfiler().getPhysicalMemoryUsage() / (size_t {1024} * size_t {1024});

        panda::log::Info("Average FPS: {}", _frameRates.front());
        panda::log::Info("CPU usage: {}", _cpuUsages.front());
        panda::log::Info("Memory usage: {}/{} MB", _physicalMemoryUsages.front(), totalPhysicalMemory);
    }
    ImGui::End();
}

}