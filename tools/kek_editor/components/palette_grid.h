#pragma once
#include <kek_palette.h>
#include <imgui.h>
#include <cmath>

// Reusable palette grid. Operates directly on KEK_palette_item data (RGB666, 0-63 per channel).
// editable=true: clicking a swatch opens a popup with per-channel sliders and a color picker.
struct PaletteGrid {
    int   selected  = -1;
    float cell_size = 18.0f;
    int   columns   = 16;

    void draw(KEK_palette_item* palette, int count, bool editable = false) {
        bool open_popup = false;

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(1, 1));

        for (int i = 0; i < count; i++) {
            if (i % columns != 0)
                ImGui::SameLine();

            ImVec4 col = to_float(palette[i]);
            bool is_sel = (selected == i);

            ImGui::PushID(i);

            if (is_sel) {
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, 1));
            }
            ImGui::PushStyleColor(ImGuiCol_Button,          col);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,   col);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,    col);

            if (ImGui::Button("##c", ImVec2(cell_size, cell_size))) {
                selected = is_sel ? -1 : i;
                if (editable && selected == i)
                    open_popup = true;
            }

            ImGui::PopStyleColor(3);
            if (is_sel) {
                ImGui::PopStyleColor(); // Border
                ImGui::PopStyleVar();   // FrameBorderSize
            }

            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::ColorButton("##prev", col, ImGuiColorEditFlags_NoTooltip, ImVec2(32, 32));
                ImGui::SameLine();
                ImGui::Text("#%03d\nR:%d G:%d B:%d",
                    i,
                    palette[i].channels.r,
                    palette[i].channels.g,
                    palette[i].channels.b);
                ImGui::EndTooltip();
            }

            ImGui::PopID();
        }

        ImGui::PopStyleVar(); // ItemSpacing

        if (open_popup)
            ImGui::OpenPopup("##color_edit");

        if (editable && ImGui::BeginPopup("##color_edit")) {
            if (selected >= 0 && selected < count)
                draw_editor(palette[selected]);
            ImGui::EndPopup();
        }
    }

private:
    void draw_editor(KEK_palette_item& entry) {
        ImGui::ColorButton("##prev", to_float(entry),
            ImGuiColorEditFlags_NoTooltip, ImVec2(24, 24));
        ImGui::SameLine();
        ImGui::Text("Entry %d", selected);

        ImGui::Spacing();

        // Per-channel sliders (0-63, 6-bit)
        int r = entry.channels.r;
        int g = entry.channels.g;
        int b = entry.channels.b;
        bool changed = false;

        ImGui::PushItemWidth(200.0f);

        ImGui::PushStyleColor(ImGuiCol_FrameBg,           ImVec4(0.3f,  0.05f, 0.05f, 1));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab,        ImVec4(1,     0.3f,  0.3f,  1));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive,  ImVec4(1,     0.5f,  0.5f,  1));
        changed |= ImGui::SliderInt("R", &r, 0, 63);
        ImGui::PopStyleColor(3);

        ImGui::PushStyleColor(ImGuiCol_FrameBg,           ImVec4(0.05f, 0.3f,  0.05f, 1));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab,        ImVec4(0.3f,  1,     0.3f,  1));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive,  ImVec4(0.5f,  1,     0.5f,  1));
        changed |= ImGui::SliderInt("G", &g, 0, 63);
        ImGui::PopStyleColor(3);

        ImGui::PushStyleColor(ImGuiCol_FrameBg,           ImVec4(0.05f, 0.05f, 0.3f,  1));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab,        ImVec4(0.3f,  0.3f,  1,     1));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive,  ImVec4(0.5f,  0.5f,  1,     1));
        changed |= ImGui::SliderInt("B", &b, 0, 63);
        ImGui::PopStyleColor(3);

        ImGui::PopItemWidth();

        if (changed) {
            entry.channels.r = (uint8_t)r;
            entry.channels.g = (uint8_t)g;
            entry.channels.b = (uint8_t)b;
        }

        ImGui::Spacing();

        // Color picker — always derived from current values so it stays in sync with sliders
        float rgb[3] = {
            entry.channels.r / 63.0f,
            entry.channels.g / 63.0f,
            entry.channels.b / 63.0f,
        };
        if (ImGui::ColorPicker3("##pick", rgb,
                ImGuiColorEditFlags_NoAlpha |
                ImGuiColorEditFlags_NoInputs |
                ImGuiColorEditFlags_PickerHueWheel)) {
            entry.channels.r = (uint8_t)roundf(rgb[0] * 63.0f);
            entry.channels.g = (uint8_t)roundf(rgb[1] * 63.0f);
            entry.channels.b = (uint8_t)roundf(rgb[2] * 63.0f);
        }
    }

    static ImVec4 to_float(KEK_palette_item item) {
        return ImVec4(
            item.channels.r / 63.0f,
            item.channels.g / 63.0f,
            item.channels.b / 63.0f,
            1.0f);
    }
};
