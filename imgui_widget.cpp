/*
 *  Copyright Vulkanite - 2022  - Thomas Simonnet
 */

#include <chrono>
#include <deque>
#include <numeric>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <implot.h>
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

#include "core_utils.h"
#include "imgui_widget.h"
#include "scene.h"

void ShowFPS() {
    static std::deque<float> fpsValues;
    static float minFPS = FLT_MAX, maxFPS = FLT_MIN;
    static auto t1 = std::chrono::high_resolution_clock::now();

    float currentFPS = std::floor(ImGui::GetIO().Framerate);
    fpsValues.push_back(currentFPS);
    if (fpsValues.size() > 100) fpsValues.pop_front();
    
    auto t2 = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = t2 - t1;

    // Update min/max every second
    if (elapsed.count() >= 1.0) {
        minFPS = *std::min_element(fpsValues.begin(), fpsValues.end());
        maxFPS = *std::max_element(fpsValues.begin(), fpsValues.end());
        t1 = t2;
    }

    float avgFPS = std::accumulate(fpsValues.begin(), fpsValues.end(), 0.0f) / fpsValues.size();

    ImGui::Columns(2, "mixed");
    ImGui::SetColumnWidth(-1, ImGui::GetWindowWidth() * 0.7f);  // Set the width of the first column to be 70% of window width

    float plotHeight = ImGui::GetTextLineHeightWithSpacing() * 4; // Get the plot height to match 4 lines of text
    ImVec2 plotSize = ImVec2(-1, plotHeight);  // -1 uses all available width
    std::vector<float> v(fpsValues.size());
    copy(fpsValues.cbegin(),fpsValues.cend(), v.begin());
    ImGui::PlotLines("FPS", &v[0], fpsValues.size(), 0, nullptr, 0, 120, plotSize);

    ImGui::NextColumn();

    ImGui::Text("Current FPS: %.1f", currentFPS);
    ImGui::Text("Min FPS: %.1f", minFPS);
    ImGui::Text("Max FPS: %.1f", maxFPS);
    ImGui::Text("Average FPS: %.1f", avgFPS);

    ImGui::Columns(1);

    ImGui::Checkbox("Rasterizer", &scene.DRAW_RASTERIZE);
    ImGui::Checkbox("FSR2", &USE_FSR2);
    ImGui::SliderFloat("Scale", &UPSCALE_SCALE, 0.5f, 1.0f);
}