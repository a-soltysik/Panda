#include "DevGui.h"

#include <fmt/ranges.h>
#include <imgui.h>
#include <implot.h>

namespace
{
[[nodiscard]] constexpr auto getLevelTag(panda::log::Level level) -> std::string_view
{
    using namespace std::string_view_literals;
    using enum panda::log::Level;
    switch (level)
    {
    case Debug:
        return "[DBG]"sv;
    case Info:
        return "[INF]"sv;
    case Warning:
        return "[WRN]"sv;
    case Error:
        return "[ERR]"sv;
    default:
        [[unlikely]] return "[???]"sv;
    }
}
}

namespace app
{

DevGui::DevGui(const panda::Window& window)
    : _window {window}
{
}

auto DevGui::render() -> void
{
    const auto windowSize =
        ImVec2 {static_cast<float>(_window.getSize().x) / 3, static_cast<float>(_window.getSize().y)};
    ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Once);
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_Once);
    ImGui::SetNextWindowCollapsed(true, ImGuiCond_Once);

    updateProfiler();
    updateLogs();

    ImGui::Begin("Debug Info", nullptr, static_cast<uint32_t>(ImGuiWindowFlags_NoScrollbar));

    if (ImGui::BeginTabBar("MyTabBar"))
    {
        for (auto i = uint32_t {}; i < _openedTabs.size(); i++)
        {
            if (_openedTabs[i] && ImGui::BeginTabItem(_tabsNames[i], &_openedTabs[i], ImGuiTabItemFlags_None))
            {
                if (i == 0)
                {
                    renderProfiler();
                }
                else
                {
                    renderLogger();
                }
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

auto DevGui::renderLogger() -> void
{
    ImGui::Checkbox("Anchor logs", &_anchorLogs);
    if (ImGui::BeginChild("Logger",
                          ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y),
                          true,
                          ImGuiWindowFlags_AlwaysHorizontalScrollbar))
    {
        for (auto i = uint32_t {}; i < _logs.size(); i++)
        {
            //NOLINTBEGIN(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
            ImGui::Text("%s %s", getLevelTag(_logs[i].level).data(), _logs[i].message.c_str());
            //NOLINTEND(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
            if (_anchorLogs && i == _logs.size() - 1)
            {
                ImGui::SetScrollHereY(1.F);
            }
        }
    }
    ImGui::EndChild();
}

auto DevGui::renderProfiler() -> void
{
    const auto plotSize = ImVec2 {ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y / 3};

    static constexpr auto maxValueOffset = 1.05;

    ImPlot::PushStyleVar(ImPlotStyleVar_PlotDefaultSize, plotSize);
    if (ImPlot::BeginPlot("Framerate"))
    {
        ImPlot::SetupLegend(ImPlotLocation_NorthEast);
        ImPlot::SetupAxes("Time [s]", "Framerate [FPS]", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_None);
        ImPlot::SetupAxesLimits(0,
                                static_cast<double>(_frameRates.size()),
                                0,
                                static_cast<double>(*std::ranges::max_element(_frameRates)) * maxValueOffset,
                                ImPlotCond_Always);
        ImPlot::PlotLine("", _frameRates.data(), static_cast<int>(_frameRates.size()));
        ImPlot::EndPlot();
    }

    if (ImPlot::BeginPlot("Memory usage"))
    {
        ImPlot::SetupLegend(ImPlotLocation_NorthEast);
        ImPlot::SetupAxes("Time [s]", "Memory usage [MB]", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_None);
        ImPlot::SetupAxesLimits(0,
                                static_cast<double>(_physicalMemoryUsages.size()),
                                0,
                                std::max(static_cast<double>(*std::ranges::max_element(_physicalMemoryUsages)),
                                         static_cast<double>(*std::ranges::max_element(_virtualMemoryUsages))) *
                                    maxValueOffset,
                                ImPlotCond_Always);
        ImPlot::PlotLine("Physical", _physicalMemoryUsages.data(), static_cast<int>(_physicalMemoryUsages.size()));
        ImPlot::PlotLine("Virtual", _virtualMemoryUsages.data(), static_cast<int>(_virtualMemoryUsages.size()));
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
}

auto DevGui::updateProfiler() -> void
{
    _time += ImGui::GetIO().DeltaTime;

    static constexpr auto second = 1.F;
    if (_time > second)
    {
        _time = 0;

        std::ranges::rotate(_physicalMemoryUsages, std::ranges::prev(std::ranges::end(_physicalMemoryUsages)));
        std::ranges::rotate(_virtualMemoryUsages, std::ranges::prev(std::ranges::end(_virtualMemoryUsages)));
        std::ranges::rotate(_frameRates, std::ranges::prev(std::ranges::end(_frameRates)));
        std::ranges::rotate(_cpuUsages, std::ranges::prev(std::ranges::end(_cpuUsages)));

        const auto physicalMemoryUsage =
            backend::getProfiler().getPhysicalMemoryUsage() / (size_t {1024} * size_t {1024});

        const auto virtualMemoryUsage =
            backend::getProfiler().getVirtualMemoryUsage() / (size_t {1024} * size_t {1024});

        _physicalMemoryUsages.front() = physicalMemoryUsage;
        _virtualMemoryUsages.front() = virtualMemoryUsage;
        _frameRates.front() = ImGui::GetIO().Framerate;
        _cpuUsages.front() = static_cast<float>(backend::getProfiler().getCpuUsageInPercents());

        const auto totalPhysicalMemory =
            backend::getProfiler().getTotalPhysicalMemory() / (size_t {1024} * size_t {1024});

        const auto totalVirtualMemory =
            backend::getProfiler().getTotalVirtualMemory() / (size_t {1024} * size_t {1024});

        panda::log::Info("Average FPS: {}", _frameRates.front());
        panda::log::Info("CPU usage: {}", _cpuUsages.front());
        panda::log::Info("Memory usage: {}/{} MB", _physicalMemoryUsages.front(), totalPhysicalMemory);
        panda::log::Info("Virtual Memory usage: {}/{} MB", _virtualMemoryUsages.front(), totalVirtualMemory);
    }
}

auto DevGui::updateLogs() -> void
{
    const auto logBuffer = panda::log::FileLogger::instance().getBuffer();
    const auto lastLog = std::ranges::find(logBuffer, _lastMaxIndex, &panda::log::FileLogger::LogData::index);
    const auto begin =
        (lastLog == std::ranges::end(logBuffer) ? std::ranges::begin(logBuffer) : std::ranges::next(lastLog));

    if (begin != std::ranges::end(logBuffer))
    {
        _lastMaxIndex = std::max(_lastMaxIndex, logBuffer.back().index);
        std::ranges::move(std::ranges::subrange(begin, std::ranges::end(logBuffer)), std::back_inserter(_logs));
    }
}

}