#pragma once
#include "../tool.h"
#include <imgui.h>

struct ModelEditor : Tool {
    const char* name() const override { return "Model Editor"; }

    void draw() override {
        // Top: 3D view
        float bottom_h = 160.0f;
        ImGui::BeginChild("##model_viewport", ImVec2(0, -bottom_h), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY);
        ImGui::Text("Model Viewport");
        ImGui::Separator();
        ImGui::Text("(model viewport)");
        ImGui::EndChild();

        // Bottom: mesh data
        ImGui::BeginChild("##mesh_data", ImVec2(0, 0), ImGuiChildFlags_Borders);
        ImGui::Text("Mesh Data");
        ImGui::Separator();
        ImGui::Text("(vertices / faces)");
        ImGui::EndChild();
    }
};
