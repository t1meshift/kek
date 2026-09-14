#pragma once
#include "tool.h"
#include <imgui.h>
#include <memory>
#include <vector>

struct Editor {
    std::vector<std::unique_ptr<Tool>> tools;
    Tool* current_tool = nullptr;

    void register_tool(std::unique_ptr<Tool> tool) {
        tools.push_back(std::move(tool));
    }

    void draw() {
        if (current_tool)
            draw_workspace();
        else
            draw_title_screen();
    }

private:
    void draw_title_screen() {
        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::Begin("##home", nullptr,
            ImGuiWindowFlags_NoTitleBar  | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove      | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoBringToFrontOnFocus);

        const float win_w  = ImGui::GetWindowWidth();
        const float win_h  = ImGui::GetWindowHeight();
        const float btn_w  = 240.0f;
        const float btn_h  = 36.0f;
        const float gap    = ImGui::GetStyle().ItemSpacing.y;
        const float total_h =
            ImGui::GetTextLineHeightWithSpacing() * 3.0f +   // title + subtitle + separator
            tools.size() * (btn_h + gap);

        ImGui::SetCursorPosY((win_h - total_h) * 0.4f);

        const char* title = "kek editor";
        ImGui::SetCursorPosX((win_w - ImGui::CalcTextSize(title).x) * 0.5f);
        ImGui::Text("%s", title);

        const char* sub = "select a tool";
        ImGui::SetCursorPosX((win_w - ImGui::CalcTextSize(sub).x) * 0.5f);
        ImGui::TextDisabled("%s", sub);

        ImGui::Spacing();
        ImGui::SetCursorPosX((win_w - btn_w) * 0.5f);
        ImGui::Separator();
        ImGui::Spacing();

        for (auto& tool : tools) {
            ImGui::SetCursorPosX((win_w - btn_w) * 0.5f);
            if (ImGui::Button(tool->name(), ImVec2(btn_w, btn_h)))
                current_tool = tool.get();
        }

        ImGui::End();
    }

    void draw_workspace() {
        Tool* tool = current_tool; // cache — menu interaction may clear current_tool

        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::SetNextWindowViewport(vp->ID);
        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoDocking         | ImGuiWindowFlags_NoTitleBar  |
            ImGuiWindowFlags_NoCollapse        | ImGuiWindowFlags_NoResize    |
            ImGuiWindowFlags_NoMove            | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
            ImGuiWindowFlags_MenuBar;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("##workspace", nullptr, flags);
        ImGui::PopStyleVar();

        if (ImGui::BeginMenuBar()) {
            if (ImGui::MenuItem("Home"))
                current_tool = nullptr;
            ImGui::Separator();
            ImGui::TextDisabled("%s", tool->name());
            ImGui::EndMenuBar();
        }

        if (current_tool)
            current_tool->draw();

        ImGui::End();
    }
};
