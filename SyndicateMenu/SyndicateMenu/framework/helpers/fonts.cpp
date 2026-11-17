#include "../headers/includes.h"
#include "imgui_freetype.h"
#include "imgui_impl_dx11.h"

void c_font::update()
{
    if (!var->gui.dpi_changed || data.empty())
        return;

    ImGuiIO& io = ImGui::GetIO();
    if (!io.Fonts || io.Fonts->Locked)
        return;

    var->gui.dpi = var->gui.stored_dpi / 100.f;

    ImFontConfig cfg;
    cfg.FontBuilderFlags = ImGuiFreeTypeBuilderFlags_ForceAutoHint | ImGuiFreeTypeBuilderFlags_Bitmap;
    cfg.FontDataOwnedByAtlas = false;

    io.Fonts->Clear();

    for (auto& font_t : data)
    {
        font_t.font = io.Fonts->AddFontFromMemoryTTF(font_t.data.data(), (int)font_t.data.size(), SCALE(font_t.size), &cfg, io.Fonts->GetGlyphRangesCyrillic());
    }

    io.Fonts->Build();
    io.FontDefault = nullptr;
    for (auto& font_t : data) {
        if (font_t.font && font_t.size >= 13.f) {
            io.FontDefault = font_t.font;
            break;
        }
    }
    if (!io.FontDefault && !data.empty())
        io.FontDefault = data[0].font;
    ImGui_ImplDX11_InvalidateDeviceObjects();
    ImGui_ImplDX11_CreateDeviceObjects();

    var->gui.dpi_changed = false;
}

ImFont* c_font::get(std::vector<unsigned char> font_data, float size)
{
    for (auto& font : data)
    {
        if (font.size == size && font.data.size() == font_data.size() && font.data == font_data)
            return font.font;
    }

    add(font_data, size);
    var->gui.dpi_changed = true;
    return nullptr;
}

void c_font::add(std::vector<unsigned char> font_data, float size)
{
    data.push_back({ font_data, size, nullptr });
}
