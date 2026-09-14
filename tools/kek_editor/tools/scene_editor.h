#pragma once
#include "../tool.h"
#include <imgui.h>

struct SceneEditor : Tool {
    const char* name() const override { return "Scene Editor"; }

    void draw() override {
        // Left column: hierarchy
        ImGui::BeginChild("##hierarchy", ImVec2(220, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
        ImGui::Text("Hierarchy");
        ImGui::Separator();
        ImGui::Text("(scene objects)");
        ImGui::EndChild();

        ImGui::SameLine();

        // Center: viewport
        ImGui::BeginChild("##viewport", ImVec2(0, 0), ImGuiChildFlags_Borders);
        ImGui::Text("Viewport");
        ImGui::Separator();
        ImGui::Text("(scene viewport)");
        ImGui::EndChild();
    }
};
