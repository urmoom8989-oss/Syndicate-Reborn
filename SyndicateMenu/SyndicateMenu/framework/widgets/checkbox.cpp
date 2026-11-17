#include "../headers/functions.h"
#include "../headers/widgets.h"

widget_t checkbox_ex(std::string_view name, bool* callback, bool keybind, key_data_t* key)
{
    struct anim_t
    {
        float offset{ 0 };
        float background_alpha{ 0.f };
        float circle_alpha{ 0.5f };
        ImVec4 background{ clr->widgets.background };
        ImVec4 circle{ clr->widgets.sub_background };
        ImVec4 text{ clr->widgets.text_inactive };

        bool window_opened{ false };
        bool window_hovered{ false };
        float window_alpha{ 0 };
        float window_offset{ 0 };
        ImVec4 icon{ clr->widgets.text_inactive };
    };

    widget_t data = gui->register_item(name, ImVec2(gui->content_avail().x, SCALE(elements->checkbox.height)));
    anim_t* anim = gui->anim_container<anim_t>(data->id);
    const ImRect rect{ data->rect.GetTR() - SCALE(elements->checkbox.size, 0), data->rect.Max };
    const ImRect add{ rect.Min - SCALE(elements->checkbox.add_size * 2, 0), rect.GetBL() - SCALE(elements->checkbox.add_size, 0) };

    data->value_changed = data->hovered && gui->mouse_clicked(mouse_button_left) && gui->is_window_hovered() && !add.Contains(gui->mouse_pos());

    if (data->value_changed)
        *callback = !*callback;

    gui->easing(anim->offset, *callback ? rect.GetWidth() - A_SCALE(elements->checkbox.radius * 2 + elements->checkbox.padding * 2) : 0, 16.f, dynamic_easing);
    gui->easing(anim->background, *callback ? clr->widgets.sub_background.Value : clr->widgets.background.Value, 16.f, dynamic_easing);
    gui->easing(anim->circle, *callback ? clr->accent.Value : clr->widgets.sub_background.Value, 16.f, dynamic_easing);
    gui->easing(anim->text, *callback ? clr->widgets.text.Value : clr->widgets.text_inactive.Value, 16.f, dynamic_easing);
    gui->easing(anim->background_alpha, *callback ? 1.f : 0.f, 6.f, static_easing);
    gui->easing(anim->circle_alpha, *callback ? 0.85f : 0.f, 6.f, static_easing);

    draw->rect_filled(gui->window_drawlist(), rect.Min, rect.Max, draw->get_clr(clr->widgets.background), rect.GetHeight() / 2);
    draw->rect_filled(gui->window_drawlist(), rect.Min, rect.Max, draw->get_clr(clr->accent, 0.15f * anim->background_alpha), rect.GetHeight() / 2);
    draw->circle_filled(gui->window_drawlist(), ImVec2(rect.Min.x + SCALE(elements->checkbox.radius + elements->checkbox.padding) + anim->offset, rect.GetCenter().y), SCALE(elements->checkbox.radius), draw->get_clr(clr->accent, 0.15f + anim->circle_alpha), 30);
    draw->text_clipped(gui->window_drawlist(), font->get(onest_medium_data, 14), data->rect.Min, rect.GetBL(), draw->get_clr(anim->text), name.data(), gui->text_end(name.data()), NULL, ImVec2(0.f, 0.5f));

    if (keybind)
    {
        gui->easing(anim->icon, add.Contains(gui->mouse_pos()) || anim->window_alpha > 0.f ? clr->widgets.text.Value : clr->widgets.text_inactive.Value, 16.f, dynamic_easing);
        gui->easing(anim->window_alpha, anim->window_opened ? 1.f : 0.f, 8.f, static_easing);
        gui->easing(anim->window_offset, anim->window_opened ? elements->keybind.window_padding : 0.f, 16.f, dynamic_easing);

        draw->text_clipped(gui->window_drawlist(), font->get(icons_data, 12), add.Min, add.Max, draw->get_clr(anim->icon), "N", NULL, NULL, ImVec2(0.5f, 0.5f));

        if ((add.Contains(gui->mouse_pos()) && gui->mouse_clicked(mouse_button_left) && gui->is_window_hovered()) || (anim->window_opened && gui->mouse_clicked(mouse_button_left) && !anim->window_hovered))
            anim->window_opened = !anim->window_opened;

        if (anim->window_alpha >= 0.01f)
        {
            gui->push_var(style_var_alpha, anim->window_alpha);
            gui->push_var(style_var_window_rounding, SCALE(elements->keybind.rounding));
            gui->push_var(style_var_popup_border_size, SCALE(1));
            gui->push_var(style_var_window_padding, SCALE(elements->keybind.padding));
            gui->push_var(style_var_item_spacing, SCALE(elements->keybind.spacing));
            gui->push_color(style_col_popup_bg, draw->get_clr(clr->window.sub_background));
            gui->push_color(style_col_border, draw->get_clr(clr->window.stroke));
            gui->set_next_window_pos(ImVec2(add.GetCenter().x - SCALE(elements->keybind.width / 2), add.Max.y + anim->window_offset));
            gui->set_next_window_size(SCALE(elements->keybind.width, 0));
            gui->begin("keybind_window" + std::to_string(data->id), nullptr, window_flags_tooltip | window_flags_always_use_window_padding | window_flags_no_saved_settings | window_flags_no_focus_on_appearing | window_flags_always_auto_resize | window_flags_no_decoration | window_flags_no_scrollbar | window_flags_no_scroll_with_mouse);
            {
                gui->set_window_focus();
                anim->window_hovered = gui->is_window_hovered();

                widgets->key_select("key_select", &key->key);

                widgets->mode_select("mode_select", &key->mode);
            }
            gui->end();
            gui->pop_color(2);
            gui->pop_var(5);
        }
    }

    var->gui.color_edit_rect = add;

    if (var->gui.section_alpha == 0.f || var->gui.sub_section_alpha == 0.f)
    {
        anim->offset = 0.f;
        anim->background_alpha = 0.f;
        anim->circle_alpha = 0.f;
        anim->text = clr->widgets.text_inactive;
    }

    return data;
}

widget_t c_widgets::checkbox(std::string_view key)
{
    checkbox_t* data = cfg->fill<checkbox_t>(key.data());

    return checkbox_ex(data->name, &data->callback, data->keybind, &data->key_data);
}