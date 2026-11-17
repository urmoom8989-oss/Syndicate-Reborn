#include "../headers/functions.h"
#include "../headers/widgets.h"

widget_t selectable(std::string_view name, bool active)
{
    struct anim_t
    {
        float alpha{ 0 };
        ImVec4 text{ clr->widgets.text_inactive };
    };

    widget_t data = gui->register_item(name, ImVec2(gui->content_avail().x, SCALE(elements->selectable.height)));
    anim_t* anim = gui->anim_container<anim_t>(data->id);
    data->value_changed = data->hovered && gui->mouse_clicked(mouse_button_left) && gui->is_window_hovered();

    gui->easing(anim->alpha, active ? 1.f : 0.f, 8.f, static_easing);
    gui->easing(anim->text, active ? clr->widgets.text.Value : clr->widgets.text_inactive.Value, 16.f, dynamic_easing);

    draw->rect_filled(gui->window_drawlist(), data->rect.Min, data->rect.Max, draw->get_clr(clr->widgets.background, anim->alpha));
    draw->text_clipped(gui->window_drawlist(), font->get(onest_medium_data, 14), data->rect.Min + SCALE(elements->selectable.padding, 0), data->rect.Max, draw->get_clr(anim->text), name.data(), gui->text_end(name.data()), NULL, ImVec2(0.f, 0.5f));

    return data;
}

bool begin_dropdown(std::string_view name, std::string& preview, int val, widget_t& data, bool multi = false)
{
    struct anim_t
    {
        bool opened{ false };
        bool hovered{ false };
        int stored_val{ -1 };
        float alpha{ 0 };
        float rotate{ 0 };
        float offset{ 0 };
    };

    data = gui->register_item(name, ImVec2(gui->content_avail().x, SCALE(elements->dropdown.height)));
    anim_t* anim = gui->anim_container<anim_t>(data->id);
    const ImRect icon{ data->rect.GetBR() - ImVec2(gui->text_size(font->get(icons_data, 6), "M").x + SCALE(elements->dropdown.padding * 2), SCALE(elements->dropdown.size)), data->rect.Max };
    const ImRect rect{ ImVec2(data->rect.Min.x, icon.Min.y), icon.GetBL() };

    if (anim->stored_val < 0)
        anim->stored_val = val;

    val = val > 4 ? 4 : val;

    if ((ImRect{ rect.Min, icon.Max }.Contains(gui->mouse_pos()) && gui->mouse_clicked(mouse_button_left) && gui->is_window_hovered()) || (anim->opened && gui->mouse_clicked(mouse_button_left) && !anim->hovered))
        anim->opened = !anim->opened;

    gui->easing(anim->alpha, anim->opened ? 1.f : 0.f, 8.f, static_easing);
    gui->easing(anim->rotate, anim->opened ? 1.f : 0.f, 16.f, dynamic_easing);
    gui->easing(anim->offset, anim->opened ? elements->dropdown.window_padding : 0.f, 16.f, dynamic_easing);

    draw->rect_filled(gui->window_drawlist(), rect.Min, icon.Max, draw->get_clr(clr->widgets.background), SCALE(elements->dropdown.rounding));
    draw->rect(gui->window_drawlist(), rect.Min, icon.Max, draw->get_clr(clr->widgets.stroke), SCALE(elements->dropdown.rounding), 0, SCALE(1));

    draw->text_clipped(gui->window_drawlist(), font->get(onest_medium_data, 14), data->rect.Min - SCALE(0, 3), rect.GetTR(), draw->get_clr(clr->widgets.text_inactive), name.data(), gui->text_end(name.data()), NULL, ImVec2(0.f, 0.f));
    draw->text_clipped(gui->window_drawlist(), font->get(onest_medium_data, 14), rect.Min + SCALE(elements->dropdown.padding, 0), icon.GetBL(), draw->get_clr(clr->widgets.text), preview.data(), NULL, NULL, ImVec2(0.0f, 0.5f));

    draw->rotate_start(gui->window_drawlist());
    draw->text_clipped(gui->window_drawlist(), font->get(icons_data, 6), icon.Min, icon.Max, draw->get_clr(clr->widgets.text), "M", NULL, NULL, ImVec2(0.5f, 0.5f));
    draw->rotate_end(gui->window_drawlist(), 90.f - (180.f * anim->rotate));

    if (anim->alpha <= 0.01f || !gui->is_rect_visible(ImRect{ rect.GetBL(), icon.GetBL() + SCALE(0, 2) }))
    {
        anim->hovered = false;

        return false;
    }

    gui->push_var(style_var_alpha, anim->alpha);
    gui->push_var(style_var_window_rounding, SCALE(elements->dropdown.rounding));
    gui->push_var(style_var_popup_border_size, SCALE(1));
    gui->push_var(style_var_window_padding, ImVec2(0, 0));
    gui->push_var(style_var_item_spacing, ImVec2(0, 0));
    gui->push_var(style_var_scrollbar_content_padding, SCALE(elements->dropdown.scrollbar_content));
    gui->push_var(style_var_scrollbar_border_padding, SCALE(elements->dropdown.scrollbar_border));
    gui->push_color(style_col_popup_bg, draw->get_clr(clr->window.sub_background));
    gui->push_color(style_col_border, draw->get_clr(clr->window.stroke));
    gui->set_next_window_pos(rect.GetBL() + SCALE(0, anim->offset));
    gui->set_next_window_size(ImVec2(data->rect.GetWidth(), SCALE(elements->selectable.height * val)));
    gui->begin("dropdown_window" + std::to_string(data->id), nullptr, window_flags_tooltip | window_flags_always_use_window_padding | window_flags_no_saved_settings | window_flags_no_focus_on_appearing | window_flags_always_auto_resize | window_flags_no_title_bar | window_flags_no_resize | window_flags_no_collapse | (anim->stored_val <= 4 ? (window_flags_no_scrollbar | window_flags_no_scroll_with_mouse) : window_flags_always_vertical_scrollbar));

    gui->set_window_focus();
    anim->hovered = gui->is_window_hovered();

    return true;
}

void end_dropdown()
{
    gui->end();
    gui->pop_color(2);
    gui->pop_var(7);
}

widget_t c_widgets::dropdown(std::string_view key)
{
    dropdown_t* data = cfg->fill<dropdown_t>(key.data());
    widget_t item_data;

    std::string preview{ data->items.at(data->callback) };

    if (begin_dropdown(data->name, preview, data->items.size(), item_data))
    {
        for (int i = 0; i < data->items.size(); ++i)
        {
            if (selectable(data->items.at(i), data->callback == i).value_changed)
            {
                data->callback = i;
                item_data->value_changed = gui->is_item_clicked(mouse_button_left);
            }
        }

        end_dropdown();
    }

    return item_data;
}

widget_t c_widgets::multi_dropdown(std::string_view key)
{
    multi_dropdown_t* data = cfg->fill<multi_dropdown_t>(key.data());
    widget_t item_data;

    std::string preview = "...";

    for (size_t i = 0; i < data->items.size(); ++i)
    {
        if (data->callback.at(i))
        {
            if (preview == "...")
                preview = data->items[i];
            else
                preview += (", ") + data->items[i];
        }
    }

    if (begin_dropdown(data->name, preview, data->items.size(), item_data, true))
    {
        for (size_t i = 0; i < data->items.size(); ++i)
        {
            if (selectable(data->items.at(i), data->callback.at(i)).value_changed)
            {
                data->callback.at(i) = !data->callback.at(i);
                item_data->value_changed = gui->is_item_clicked(mouse_button_left);
            }
        }
        end_dropdown();
    }

    preview = ("...");

    return item_data;
}