#include "../headers/functions.h"
#include "../headers/widgets.h"

#include "../headers/functions.h"
#include "../headers/widgets.h"

const char* const keys[] = { "Tab", "Left", "Right", "Up", "Down", "Page Up", "Page Down", "Home", "End", "Insert", "Delete", "Backspace", "Space", "Enter", "Escape", "Ctrl", "Shift", "Alt", "Super", "Ctrl", "Shift", "Alt", "Super", "Menu", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12", "F13", "F14", "F15", "F16", "F17", "F18", "F19", "F20", "F21", "F22", "F23", "F24", "Apostrophe", "Comma", "Minus", "Period", "Slash", "Semicolon", "Equal", "Left Bracket", "Backslash", "Right Bracket", "Grave Accent", "Caps Lock", "Scroll Lock", "Num Lock", "Print Screen", "Pause", "Keypad 0", "Keypad 1", "Keypad 2", "Keypad 3", "Keypad 4", "Keypad 5", "Keypad 6", "Keypad 7", "Keypad 8", "Keypad 9", "Keypad .", "Keypad /", "Keypad *", "Keypad -", "Keypad +", "Keypad Enter", "Keypad =", "App Back", "App Forward", "Gamepad Start", "Gamepad Back", "Gamepad Face Left", "Gamepad Face Right", "Gamepad Face Up", "Gamepad Face Down", "Gamepad Dpad Left", "Gamepad Dpad Right", "Gamepad Dpad Up", "Gamepad Dpad Down", "Gamepad L1", "Gamepad R1", "Gamepad L2", "Gamepad R2", "Gamepad L3", "Gamepad R3", "Gamepad L Stick Left", "Gamepad L Stick Right", "Gamepad L Stick Up", "Gamepad L Stick Down", "Gamepad R Stick Left", "Gamepad R Stick Right", "Gamepad R Stick Up", "Gamepad R Stick Down", "Mouse 1", "Mouse 2", "Mouse 3", "Mouse 4", "Mouse 5", "Mouse Wheel X", "Mouse Wheel Y", "Ctrl", "Shift", "Alt", "Super" };

const char* get_key_name(ImGuiKey key)
{
    if (key == ImGuiKey_None)
        return "...";

    return keys[key - ImGuiKey_NamedKey_BEGIN];
}

widget_t c_widgets::key_select(std::string_view name, int* key)
{
    struct anim_t
    {
        bool active{ false };
        float alpha{ 0 };
        ImVec4 text{ clr->widgets.text };
    };

    widget_t data = gui->register_item(name, ImVec2(gui->content_avail().x, SCALE(elements->keybind.height)));
    anim_t* anim = gui->anim_container<anim_t>(data->id);
    std::string buf_display = anim->active ? "..." : get_key_name((ImGuiKey)*key);

    if (data->hovered && gui->mouse_released(mouse_button_left) && gui->is_window_hovered())
        anim->active = true;

    if (anim->active)
    {
        for (int i = 0; i < ImGuiKey_COUNT; ++i)
        {

            if (IsKeyPressed(ImGuiKey_Escape))
            {
                *key = 0;
                anim->active = false;
            }

            if (IsKeyPressed((ImGuiKey)i))
            {
                *key = i;
                anim->active = false;
            }
        }
    }

    gui->easing(anim->alpha, anim->active ? 1.f : 0.f, 8.f, static_easing);

    draw->rect_filled(gui->window_drawlist(), data->rect.Min, data->rect.Max, draw->get_clr(clr->widgets.background), SCALE(elements->keybind.rounding));
    draw->rect_filled(gui->window_drawlist(), data->rect.Min, data->rect.Max, draw->get_clr(clr->accent, 0.2f * anim->alpha), SCALE(elements->keybind.rounding));
    draw->rect(gui->window_drawlist(), data->rect.Min, data->rect.Max, draw->get_clr(clr->widgets.stroke), SCALE(elements->keybind.rounding), 0, SCALE(1));

    draw->text_clipped(gui->window_drawlist(), font->get(onest_medium_data, 14), data->rect.Min, data->rect.Max, draw->get_clr(clr->widgets.text), buf_display.data(), NULL, NULL, ImVec2(0.5f, 0.5f));

    return data;
}

widget_t c_widgets::mode_select(std::string_view name, int* mode)
{
    struct anim_t
    {
        float alpha[2]{ 0, 0 };
        ImVec4 text[2]{ clr->widgets.text, clr->widgets.text };
    };

    widget_t data = gui->register_item(name, ImVec2(gui->content_avail().x, SCALE(elements->keybind.height)));
    anim_t* anim = gui->anim_container<anim_t>(data->id);
    const ImRect toggle{ data->rect.Min, ImVec2(data->rect.GetCenter().x - var->style.item_spacing.x / 2, data->rect.Max.y) };
    const ImRect hold{ ImVec2(data->rect.GetCenter().x + var->style.item_spacing.x / 2, data->rect.Min.y), data->rect.Max };

    if (toggle.Contains(gui->mouse_pos()) && gui->mouse_clicked(mouse_button_left) && gui->is_window_hovered())
        *mode = 0;
    if (hold.Contains(gui->mouse_pos()) && gui->mouse_clicked(mouse_button_left) && gui->is_window_hovered())
        *mode = 1;

    gui->easing(anim->alpha[0], *mode == 0 ? 1.f : 0.f, 8.f, static_easing);
    gui->easing(anim->alpha[1], *mode == 1 ? 1.f : 0.f, 8.f, static_easing);

    gui->easing(anim->text[0], *mode == 0 ? clr->widgets.background.Value : clr->widgets.text.Value, 14.f, dynamic_easing);
    gui->easing(anim->text[1], *mode == 1 ? clr->widgets.background.Value : clr->widgets.text.Value, 14.f, dynamic_easing);

    draw->rect_filled(gui->window_drawlist(), toggle.Min, toggle.Max, draw->get_clr(clr->widgets.background), SCALE(elements->keybind.rounding));
    draw->rect(gui->window_drawlist(), toggle.Min, toggle.Max, draw->get_clr(clr->widgets.stroke), SCALE(elements->keybind.rounding), 0, SCALE(1));
    draw->rect_filled(gui->window_drawlist(), toggle.Min, toggle.Max, draw->get_clr(clr->accent, anim->alpha[0]), SCALE(elements->keybind.rounding));
    draw->text_clipped(gui->window_drawlist(), font->get(onest_medium_data, 14), toggle.Min, toggle.Max, draw->get_clr(anim->text[0]), "Toggle", NULL, NULL, ImVec2(0.5f, 0.5f));

    draw->rect_filled(gui->window_drawlist(), hold.Min, hold.Max, draw->get_clr(clr->widgets.background), SCALE(elements->keybind.rounding));
    draw->rect(gui->window_drawlist(), hold.Min, hold.Max, draw->get_clr(clr->widgets.stroke), SCALE(elements->keybind.rounding), 0, SCALE(1));
    draw->rect_filled(gui->window_drawlist(), hold.Min, hold.Max, draw->get_clr(clr->accent, anim->alpha[1]), SCALE(elements->keybind.rounding));
    draw->text_clipped(gui->window_drawlist(), font->get(onest_medium_data, 14), hold.Min, hold.Max, draw->get_clr(anim->text[1]), "Hold", NULL, NULL, ImVec2(0.5f, 0.5f));

    return data;
}