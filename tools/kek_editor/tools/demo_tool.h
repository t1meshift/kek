#pragma once
#include "../tool.h"
#include "../components/palette_grid.h"
#include <kek_palette.h>
#include <imgui.h>
#include <cstring>

struct DemoTool : Tool {
    const char* name() const override { return "Demo"; }

    DemoTool() {
        std::memcpy(palette, KEK_DEFAULT_PALETTE, sizeof(palette));
    }

    void draw() override {
        // Left: 256-color palette (editable)
        ImGui::BeginChild("##palette256", ImVec2(380, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
        ImGui::Text("Palette (256, editable)");
        ImGui::Separator();
        grid256.draw(palette, 256, true);
        ImGui::EndChild();

        ImGui::SameLine();

        // Right: first 16 colors, read-only, bigger swatches
        ImGui::BeginChild("##palette16", ImVec2(0, 0), ImGuiChildFlags_Borders);
        ImGui::Text("First 16 colors (read-only)");
        ImGui::Separator();
        grid16.draw(palette, 16, false);
        ImGui::EndChild();
    }

private:
    KEK_palette_item palette[256];
    PaletteGrid grid256;
    PaletteGrid grid16{ .cell_size = 32.0f, .columns = 8 };
};
