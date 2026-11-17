#include "../headers/functions.h"
#include "../headers/widgets.h"
#include <wtypes.h>

widget_t picker_button(std::string_view name, bool active)
{
    struct anim_t
    {
        float alpha{ 0 };
        ImVec4 text{ clr->widgets.text_inactive };
    };

    widget_t data = gui->register_item(name, ImVec2(gui->content_avail().x, SCALE(elements->color_edit.button_height)));
    anim_t* anim = gui->anim_container<anim_t>(data->id);

    data->value_changed = data->hovered && gui->mouse_clicked(mouse_button_left) && gui->is_window_hovered();

    gui->easing(anim->alpha, active ? 1.f : 0.f, 8.f, static_easing);
    gui->easing(anim->text, active ? clr->widgets.background.Value : clr->widgets.text.Value, 16.f, dynamic_easing);

    draw->rect_filled(gui->window_drawlist(), data->rect.Min, data->rect.Max, draw->get_clr(clr->widgets.background), SCALE(elements->color_edit.content_rounding));
    draw->rect(gui->window_drawlist(), data->rect.Min, data->rect.Max, draw->get_clr(clr->widgets.stroke), SCALE(elements->color_edit.content_rounding), 0, SCALE(1));
    draw->rect_filled(gui->window_drawlist(), data->rect.Min, data->rect.Max, draw->get_clr(clr->accent, anim->alpha), SCALE(elements->color_edit.content_rounding));
    draw->text_clipped(gui->window_drawlist(), font->get(icons_data, 10), data->rect.Min, data->rect.Max, draw->get_clr(anim->text), "O", NULL, NULL, ImVec2(0.5f, 0.5f));

    return data;
}

widget_t type_button(std::string_view name)
{
    struct anim_t
    {
        float active{ false };
        float alpha{ 0 };
        ImVec4 text{ clr->widgets.text_inactive };
    };

    widget_t data = gui->register_item(name, ImVec2(gui->content_avail().x, SCALE(elements->color_edit.button_height)));
    anim_t* anim = gui->anim_container<anim_t>(data->id);

    data->value_changed = data->hovered && gui->mouse_clicked(mouse_button_left) && gui->is_window_hovered();

    if (data->value_changed || data->hovered && gui->mouse_down(mouse_button_left) && gui->is_window_hovered())
        anim->active = true;

    gui->easing(anim->alpha, anim->active ? 1.f : 0.f, 8.f, static_easing);
    gui->easing(anim->text, anim->active ? clr->widgets.background.Value : clr->widgets.text.Value, 16.f, dynamic_easing);

    if (anim->alpha >= 0.99f)
        anim->active = false;

    draw->rect_filled(gui->window_drawlist(), data->rect.Min, data->rect.Max, draw->get_clr(clr->widgets.background), SCALE(elements->color_edit.content_rounding));
    draw->rect(gui->window_drawlist(), data->rect.Min, data->rect.Max, draw->get_clr(clr->widgets.stroke), SCALE(elements->color_edit.content_rounding), 0, SCALE(1));
    draw->rect_filled(gui->window_drawlist(), data->rect.Min, data->rect.Max, draw->get_clr(clr->accent, anim->alpha), SCALE(elements->color_edit.content_rounding));
    draw->text_clipped(gui->window_drawlist(), font->get(onest_medium_data, 14), data->rect.Min, data->rect.Max, draw->get_clr(anim->text), name.data(), NULL, NULL, ImVec2(0.5f, 0.5f));

    return data;
}

bool input_hex(const char* label, float col[4], bool alpha, float width)
{
    char buf[9];

    if (alpha)
        snprintf(buf, sizeof(buf), "%02X%02X%02X%02X",
            (int)(col[0] * 255.0f),
            (int)(col[1] * 255.0f),
            (int)(col[2] * 255.0f),
            (int)(col[3] * 255.0f));
    else
        snprintf(buf, sizeof(buf), "%02X%02X%02X",
            (int)(col[0] * 255.0f),
            (int)(col[1] * 255.0f),
            (int)(col[2] * 255.0f));

    bool value_changed = widgets->hex_field(label, buf, sizeof(buf), width).value_changed;

    if (value_changed)
    {
        if (buf[0] == '#')
        {
            memmove(buf, buf + 1, strlen(buf) + 1);
        }

        for (int i = 0; i < (int)strlen(buf); ++i)
        {
            if (!isxdigit((unsigned char)buf[i]))
            {
                buf[i] = 'F';
            }
        }

        int expected_len = alpha ? 8 : 6;
        size_t current_len = strlen(buf);
        if (current_len < (size_t)expected_len)
        {
            for (size_t i = current_len; i < (size_t)expected_len; ++i)
                buf[i] = 'F';
            buf[expected_len] = '\0';
        }

        int r, g, b, a = 255;
        if (alpha)
            sscanf(buf, "%02X%02X%02X%02X", &r, &g, &b, &a);
        else
            sscanf(buf, "%02X%02X%02X", &r, &g, &b);

        col[0] = r / 255.0f;
        col[1] = g / 255.0f;
        col[2] = b / 255.0f;
        col[3] = a / 255.0f;
    }

    return value_changed;
}

void color_edit_ex(std::string_view name, float* color, bool alpha, const ImRect& rect, const ImRect& clickable_rect, bool* value_changed)
{
    struct anim_t
    {
        bool opened{ false };
        bool hovered{ false };
        float alpha{ 0.f };
        float offset{ 0 };
        float h{ -1 }, s{ -1 }, v{ -1 };
        float grab[4]{ 0.f };
        bool update_hsv{ false };
        bool pick_active{ false };
        ImVec2 end_pos{};
        int type{ 0 };
    };

    anim_t* anim = gui->anim_container<anim_t>(gui->get_window()->GetID(name.data()));
    ImColor col_hues[7] = { ImColor(255, 0, 0), ImColor(255, 255, 0), ImColor(0, 255, 0), ImColor(0, 255, 255), ImColor(0, 0, 255), ImColor(255, 0, 255), ImColor(255, 0, 0) };

    if ((clickable_rect.Contains(gui->mouse_pos()) && gui->mouse_clicked(mouse_button_left)) && gui->is_window_hovered() || (anim->opened && (gui->mouse_clicked(mouse_button_left) || gui->mouse_clicked(mouse_button_right)) && !anim->hovered && !anim->pick_active))
        anim->opened = !anim->opened;

    gui->easing(anim->alpha, anim->opened ? 1.f : 0.f, 8.f, static_easing);
    gui->easing(anim->offset, anim->opened ? elements->color_edit.window_padding : 0.f, 16.f, dynamic_easing);

    draw->text_clipped(gui->window_drawlist(), font->get(icons_data, 12), rect.Min, rect.Max, draw->get_clr({ color[0], color[1], color[2], color[3] }), "P", NULL, NULL, ImVec2(0.5f, 0.5f));

    gui->rgb_to_hsv(color[0], color[1], color[2], anim->h, anim->s, anim->v);

    if (anim->alpha >= 0.01f)
    {
        gui->push_var(style_var_alpha, anim->alpha);
        gui->push_var(style_var_window_rounding, SCALE(elements->color_edit.rounding));
        gui->push_var(style_var_popup_border_size, SCALE(1));
        gui->push_var(style_var_window_padding, SCALE(elements->color_edit.padding));
        gui->push_var(style_var_item_spacing, SCALE(elements->color_edit.spacing));
        gui->push_color(style_col_popup_bg, draw->get_clr(clr->window.sub_background));
        gui->push_color(style_col_border, draw->get_clr(clr->window.stroke));
        gui->set_next_window_pos(ImVec2(rect.GetCenter().x - SCALE(elements->color_edit.width / 2), rect.Max.y + SCALE(anim->offset)));
        gui->set_next_window_size(SCALE(elements->color_edit.width, 0));
        gui->begin("coloredit_window" + std::string(name), nullptr, window_flags_tooltip | window_flags_always_use_window_padding | window_flags_no_saved_settings | window_flags_no_focus_on_appearing | window_flags_always_auto_resize | window_flags_no_decoration | window_flags_no_scrollbar | window_flags_no_scroll_with_mouse);
        {
            gui->set_window_focus();
            anim->hovered = gui->is_window_hovered();

            gui->dummy(ImVec2(gui->content_avail().x, SCALE(elements->color_edit.title - elements->color_edit.spacing.y)));
            draw->rect_filled(gui->window_drawlist(), gui->window_pos(), gui->window_pos() + ImVec2(gui->window_width(), SCALE(elements->color_edit.title)), draw->get_clr(clr->widgets.background), SCALE(elements->color_edit.rounding), draw_flags_round_corners_top);
            draw->line(gui->window_drawlist(), gui->window_pos() + SCALE(0, elements->color_edit.title - 1), gui->window_pos() + ImVec2(gui->window_width(), SCALE(elements->color_edit.title) - 1), draw->get_clr(clr->widgets.stroke), SCALE(1));
            draw->text_clipped(gui->window_drawlist(), font->get(onest_medium_data, 14), gui->window_pos() + SCALE(elements->color_edit.padding.x, 0), gui->window_pos() + ImVec2(gui->window_width(), SCALE(elements->color_edit.title)), draw->get_clr(clr->widgets.text), name.data(), gui->text_end(name.data()), NULL, ImVec2(0.f, 0.5f));

            // sv_rect
            gui->invisible_button("sv_rect", ImVec2(gui->content_avail().x, gui->content_avail().x));

            if (gui->is_item_active())
            {
                anim->s = ImSaturate((gui->mouse_pos().x - (GImGui->LastItemData.Rect.Min.x + SCALE(elements->color_edit.circle_size) / 2)) / (GImGui->LastItemData.Rect.GetWidth() - SCALE(elements->color_edit.circle_size)));
                anim->v = 1.f - ImSaturate((gui->mouse_pos().y - (GImGui->LastItemData.Rect.Min.y + SCALE(elements->color_edit.circle_size) / 2)) / (GImGui->LastItemData.Rect.GetHeight() - SCALE(elements->color_edit.circle_size)));
                *value_changed = true;
            }

            gui->easing(anim->grab[0], SCALE(elements->color_edit.circle_size) / 2 + anim->s * (GImGui->LastItemData.Rect.GetWidth() - SCALE(elements->color_edit.circle_size)), 20.f, dynamic_easing);
            gui->easing(anim->grab[1], SCALE(elements->color_edit.circle_size) / 2 + (1.f - anim->v) * (GImGui->LastItemData.Rect.GetHeight() - SCALE(elements->color_edit.circle_size)), 20.f, dynamic_easing);

            float R, G, B;
            ColorConvertHSVtoRGB(anim->h, 1.f, 1.f, R, G, B);

            draw->rect_filled_multi_color(gui->window_drawlist(), GImGui->LastItemData.Rect.Min, GImGui->LastItemData.Rect.Max, draw->get_clr({ 1.f, 1.f, 1.f, 1.f }), draw->get_clr({ R, G, B, 1.f }), draw->get_clr({ R, G, B, 1.f }), draw->get_clr({ 1.f, 1.f, 1.f, 1.f }), SCALE(elements->color_edit.content_rounding));
            draw->rect_filled_multi_color(gui->window_drawlist(), GImGui->LastItemData.Rect.Min, GImGui->LastItemData.Rect.Max, draw->get_clr({ 0.f, 0.f, 0.f, 0.f }), draw->get_clr({ 0.f, 0.f, 0.f, 0.f }), draw->get_clr({ 0.f, 0.f, 0.f, 1.f }), draw->get_clr({ 0.f, 0.f, 0.f, 1.f }), SCALE(elements->color_edit.content_rounding - 1));

            draw->shadow_circle(gui->window_drawlist(), GImGui->LastItemData.Rect.Min + ImVec2(anim->grab[0], anim->grab[1]), SCALE(elements->color_edit.circle_size / 2), draw->get_clr({ 0.f, 0.f, 0.f, 1.f }), SCALE(15), ImVec2(0, 0), draw_flags_shadow_cut_out_shape_background);
            draw->circle(gui->window_drawlist(), GImGui->LastItemData.Rect.Min + ImVec2(anim->grab[0], anim->grab[1]), SCALE(elements->color_edit.circle_size / 2), draw->get_clr(clr->widgets.text), 30, SCALE(1));

            gui->begin_group();
            {
                // hue_bar
                if (!alpha)
                    gui->set_pos(gui->get_pos().y + SCALE(elements->color_edit.button_height / 2 - (elements->color_edit.bar_size.y - elements->color_edit.grab_padding * 2) / 2), pos_y);

                gui->invisible_button("hue_bar", SCALE(elements->color_edit.bar_size));

                if (gui->is_item_active())
                {
                    anim->h = ImSaturate((gui->mouse_pos().x - (GImGui->LastItemData.Rect.Min.x + SCALE(elements->color_edit.grab_width) / 2)) / (GImGui->LastItemData.Rect.GetWidth() - SCALE(elements->color_edit.grab_width)));
                    *value_changed = true;
                }

                gui->easing(anim->grab[2], SCALE(elements->color_edit.grab_width) / 2 + anim->h * (GImGui->LastItemData.Rect.GetWidth() - SCALE(elements->color_edit.grab_width)), 20.f, dynamic_easing);

                for (int i = 0; i < IM_ARRAYSIZE(col_hues) - 1; ++i)
                    draw->rect_filled_multi_color(gui->window_drawlist(), GImGui->LastItemData.Rect.Min + ImVec2(roundf(i * (GImGui->LastItemData.Rect.GetWidth() / 6)), SCALE(elements->color_edit.grab_padding)), ImVec2(GImGui->LastItemData.Rect.Min.x + roundf((i + 1) * (GImGui->LastItemData.Rect.GetWidth() / 6)), GImGui->LastItemData.Rect.Max.y - SCALE(elements->color_edit.grab_padding)), draw->get_clr(col_hues[i]), draw->get_clr(col_hues[i + 1]), draw->get_clr(col_hues[i + 1]), draw->get_clr(col_hues[i]), SCALE(10), i == 0 ? ImDrawFlags_RoundCornersLeft : i == 5 ? ImDrawFlags_RoundCornersRight : ImDrawFlags_RoundCornersNone);

                draw->rect_filled(gui->window_drawlist(), ImVec2(GImGui->LastItemData.Rect.Min.x + anim->grab[2] - SCALE(elements->color_edit.grab_width / 2), GImGui->LastItemData.Rect.Min.y), ImVec2(GImGui->LastItemData.Rect.Min.x + anim->grab[2] + SCALE(elements->color_edit.grab_width / 2), GImGui->LastItemData.Rect.Max.y), draw->get_clr(clr->widgets.text), SCALE(elements->color_edit.grab_width / 2));

                gui->set_screen_pos(GImGui->LastItemData.Rect.Max.y + SCALE(14), pos_y);

                // alpha_bar
                if (alpha)
                {
                    gui->invisible_button("alpha_bar", SCALE(elements->color_edit.bar_size));

                    if (gui->is_item_active())
                        color[3] = ImSaturate((gui->mouse_pos().x - (GImGui->LastItemData.Rect.Min.x + SCALE(elements->color_edit.grab_width) / 2)) / (GImGui->LastItemData.Rect.GetWidth() - SCALE(elements->color_edit.grab_width)));

                    gui->easing(anim->grab[3], SCALE(elements->color_edit.grab_width) / 2 + color[3] * (GImGui->LastItemData.Rect.GetWidth() - SCALE(elements->color_edit.grab_width)), 20.f, dynamic_easing);

                    draw->rect_filled_multi_color(gui->window_drawlist(), GImGui->LastItemData.Rect.Min + SCALE(0, elements->color_edit.grab_padding), GImGui->LastItemData.Rect.Max - SCALE(0, elements->color_edit.grab_padding), draw->get_clr({ 0.f, 0.f, 0.f, 1.f }), draw->get_clr({ R, G, B, 1.f }), draw->get_clr({ R, G, B, 1.f }), draw->get_clr({ 0.f, 0.f, 0.f, 1.f }), SCALE(elements->color_edit.rounding));

                    draw->rect_filled(gui->window_drawlist(), ImVec2(GImGui->LastItemData.Rect.Min.x + anim->grab[3] - SCALE(elements->color_edit.grab_width / 2), GImGui->LastItemData.Rect.Min.y), ImVec2(GImGui->LastItemData.Rect.Min.x + anim->grab[3] + SCALE(elements->color_edit.grab_width / 2), GImGui->LastItemData.Rect.Max.y), draw->get_clr(clr->widgets.text), SCALE(elements->color_edit.grab_width / 2));
                }

            }
            gui->end_group();

            gui->sameline();

            if (picker_button("##picker", anim->pick_active).value_changed && !anim->pick_active)
                anim->pick_active = !anim->pick_active;

            if (anim->pick_active && gui->mouse_clicked(mouse_button_left) && !ImRect(gui->window_pos(), gui->window_pos() + gui->window_size()).Contains(gui->mouse_pos()))
            {
                HDC hdcScreen = GetDC(NULL);
                COLORREF pick_color = GetPixel(hdcScreen, GetMousePos().x, GetMousePos().y);
                ReleaseDC(NULL, hdcScreen);

                color[0] = GetRValue(pick_color) / 255.0f;
                color[1] = GetGValue(pick_color) / 255.0f;
                color[2] = GetBValue(pick_color) / 255.0f;

                anim->pick_active = false;
            }

            draw->rect_filled(gui->window_drawlist(), gui->get_screen_pos(), anim->end_pos, draw->get_clr(clr->widgets.background), SCALE(elements->color_edit.content_rounding));
            draw->rect(gui->window_drawlist(), gui->get_screen_pos(), anim->end_pos, draw->get_clr(clr->widgets.stroke), SCALE(elements->color_edit.content_rounding), 0, SCALE(1));

            if (anim->type == 0)
            {
                char buf[4][5];
                for (int i = 0; i < 3 + alpha; ++i)
                {
                    snprintf(buf[i], sizeof(buf[i]), "%d", (int)(ImClamp(color[i] * (i < 3 ? 255.0f : 100.0f), 0.f, (i < 3 ? 255.0f : 100.0f))));

                    const char* label = (i == 0) ? "r_col" : (i == 1) ? "g_col" : (i == 2) ? "b_col" : "a_col";
                    if (widgets->picker_field(label, buf[i], sizeof(buf[i]), SCALE(alpha ? 32 : 43)).value_changed)
                    {
                        std::string str = buf[i];
                        if (!str.empty() && str.back() == '%')
                            str.pop_back();

                        if (!str.empty())
                            color[i] = std::stoi(str) / (i < 3 ? 255.0f : 100.0f);
                        else
                            color[i] = 0.f;
                    }

                    if (i < 3)
                    {
                        gui->sameline(0, SCALE(1));
                        draw->rect_filled(gui->window_drawlist(), ImVec2(GImGui->LastItemData.Rect.Max.x, GImGui->LastItemData.Rect.GetCenter().y - SCALE(4)), ImVec2(GImGui->LastItemData.Rect.Max.x + SCALE(1), GImGui->LastItemData.Rect.GetCenter().y + SCALE(4)), draw->get_clr(clr->widgets.stroke));
                    }
                }

                anim->end_pos = GImGui->LastItemData.Rect.Max;
            }
            else
            {
                if (input_hex("#HEX", color, alpha, SCALE(131)))
                    anim->update_hsv = true;
            }

            gui->sameline();

            if (type_button(anim->type == 0 ? "RGB" : "HEX").value_changed)
                anim->type ^= 1;

        }
        gui->end();
        gui->pop_color(2);
        gui->pop_var(5);
    }

    anim->h = ImClamp(anim->h, 0.001f, 0.999f);
    anim->s = ImClamp(anim->s, 0.001f, 0.999f);
    anim->v = ImClamp(anim->v, 0.001f, 0.999f);

    if (*value_changed)
    {
        gui->hsv_to_rgb(anim->h, anim->s, anim->v, color[0], color[1], color[2]);
        *value_changed = false;
    }
}

bool c_widgets::color_edit(std::string_view key)
{
    color_edit_t* data = cfg->fill<color_edit_t>(key.data());
    bool* value_changed = gui->anim_container<bool>(gui->get_window()->GetID((data->name.data() + std::string("_colorbox")).data()));

    color_edit_ex(data->name, data->color.data(), data->alpha, var->gui.color_edit_rect, var->gui.color_edit_rect, value_changed);

    var->gui.color_edit_rect = ImRect(var->gui.color_edit_rect.Min - SCALE(elements->checkbox.add_size * 2, 0), var->gui.color_edit_rect.GetBL() - SCALE(elements->checkbox.add_size, 0));

    return *value_changed;
}