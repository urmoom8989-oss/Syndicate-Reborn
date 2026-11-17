#include "../headers/functions.h"
#include "../headers/widgets.h"

widget_t c_widgets::section(std::string_view icon, int index, int& count)
{
    struct anim_t
    {
        float alpha{ 0 };
        ImVec4 icon{ clr->widgets.text_inactive };
    };

    widget_t data = gui->register_item("section_" + std::string(icon), ImVec2(gui->content_avail().x, gui->content_avail().x));
    anim_t* anim = gui->anim_container<anim_t>(data->id);
    bool active = index == count;

    data->value_changed = data->hovered && gui->mouse_clicked(mouse_button_left) && gui->is_window_hovered();
    if (data->value_changed)
    {
        count = index;
    }

    gui->easing(anim->alpha, active ? 1.f : 0.f, 8.f, static_easing);
    gui->easing(anim->icon, active ? clr->accent.Value : clr->widgets.text_inactive.Value, 16.f, dynamic_easing);

    draw->text_clipped(gui->window_drawlist(), font->get(icons_data, 12.f), data->rect.Min, data->rect.Max, draw->get_clr(anim->icon), icon.data(), NULL, NULL, ImVec2(0.5f, 0.5f));

    return data;
}

void c_widgets::begin_section(std::string_view name)
{
    struct anim_t
    {
        float height{ 0 };
    };

    anim_t* anim = gui->anim_container<anim_t>(gui->get_window()->GetID(("section_" + std::string(name)).data()));
    widget_t data = gui->register_item("section_" + std::string(name), ImVec2(gui->content_avail().x, SCALE(elements->sub_section.top + elements->sub_section.bottom) + anim->height));

    const float circle_icon_size = gui->text_size(font->get(icons_data, 10), "K").x;
    draw->text_clipped(var->gui.drawlist, font->get(icons_data, 10), data->rect.Min, data->rect.Max, draw->get_clr(clr->accent), "K", NULL, NULL, ImVec2(0.f, 0.f));
    draw->text_clipped(var->gui.drawlist, font->get(onest_medium_data, 14), data->rect.Min + ImVec2(circle_icon_size + SCALE(elements->sub_section.padding.x), -SCALE(2)), data->rect.Max, draw->get_clr(clr->widgets.text), name.data(), gui->text_end(name.data()), NULL, ImVec2(0.f, 0.f));

    draw->push_clip_rect(var->gui.drawlist, data->rect.Min + ImVec2(circle_icon_size / 2 - 1, circle_icon_size + SCALE(elements->sub_section.padding.y + 1)), ImVec2(data->rect.Min.x + circle_icon_size / 2 + SCALE(9), data->rect.Max.y + 1), true);
    draw->rect(var->gui.drawlist, data->rect.Min + ImVec2(circle_icon_size / 2, circle_icon_size + SCALE(elements->sub_section.padding.y)), ImVec2(data->rect.Min.x + circle_icon_size / 2 + SCALE(10), data->rect.Max.y - SCALE(5)), draw->get_clr(clr->widgets.sub_stroke), SCALE(elements->sub_section.rounding), draw_flags_round_corners_bottom_left, SCALE(1));
    draw->pop_clip_rect(var->gui.drawlist);

    gui->set_screen_pos(data->rect.Min + ImVec2(circle_icon_size + SCALE(elements->sub_section.padding.x), SCALE(elements->sub_section.top)), pos_all);
    gui->begin_content("section_content_" + std::string(name), ImVec2(gui->content_avail().x, anim->height), ImVec2(0, 0), SCALE(0, 0), window_flags_no_scrollbar | window_flags_no_scroll_with_mouse);

    anim->height = gui->get_window()->ContentSize.y;

}

void c_widgets::end_section()
{
    gui->end_content();
}

bool c_widgets::begin_sub_section(std::string_view name)
{
    struct anim_t
    {
        float height{ 0 };
        float anim_height{ 1 };
        float data_height{ 3 };
        float alpha{ 0 };
        float rotate{ 0 };
        bool opened{ false };
        ImVec4 text{ clr->widgets.text_inactive };
        ImVec4 icon{ clr->widgets.text_inactive };
    };

    const float rect_icon_size = gui->text_size(font->get(icons_data, 10), "L").x;

    anim_t* anim = gui->anim_container<anim_t>(gui->get_window()->GetID(("sub_section_" + std::string(name)).data()));
    widget_t data = gui->register_item("sub_section_" + std::string(name), ImVec2(gui->content_avail().x, rect_icon_size + anim->data_height));

    var->gui.sub_pos = data->rect.GetBL() + SCALE(0, elements->sub_section.item_spacing);

    gui->easing(anim->anim_height, anim->opened ? anim->height : 1.f, 16.f, dynamic_easing);
    gui->easing(anim->data_height, anim->opened ? anim->height + SCALE(elements->sub_section.padding.y) : 3.f, 16.f, dynamic_easing);
    gui->easing(anim->alpha, anim->opened ? 1.f : 0.f, 8.f, static_easing);
    gui->easing(anim->rotate, anim->opened ? 1.f : 0.f, 16.f, dynamic_easing);
    gui->easing(anim->text, anim->opened ? clr->widgets.text.Value : clr->widgets.text_inactive.Value, 16.f, dynamic_easing);
    gui->easing(anim->icon, anim->opened ? clr->accent.Value : clr->widgets.text_inactive.Value, 16.f, dynamic_easing);

    if (ImRect{ data->rect.Min, data->rect.GetTR() + ImVec2(0, rect_icon_size) }.Contains(gui->mouse_pos()) && gui->mouse_clicked(mouse_button_left))
        anim->opened = !anim->opened;

    draw->rotate_start(var->gui.drawlist);
    draw->text_clipped(var->gui.drawlist, font->get(icons_data, 5), data->rect.Min, data->rect.GetTR() + ImVec2(0, rect_icon_size), draw->get_clr(anim->text), "M", NULL, NULL, ImVec2(1.f, 0.5f));
    draw->rotate_end(var->gui.drawlist, -90 + (180 * anim->rotate));

    draw->text_clipped(var->gui.drawlist, font->get(icons_data, 10), data->rect.Min, data->rect.Max, draw->get_clr(anim->icon), "L", NULL, NULL, ImVec2(0.f, 0.f));
    draw->text_clipped(var->gui.drawlist, font->get(onest_medium_data, 14), data->rect.Min + ImVec2(rect_icon_size + SCALE(elements->sub_section.padding.x), -SCALE(2)), data->rect.Max, draw->get_clr(anim->text), name.data(), gui->text_end(name.data()), NULL, ImVec2(0.f, 0.f));

    draw->push_clip_rect(var->gui.drawlist, data->rect.Min + ImVec2(rect_icon_size / 2 - 1, rect_icon_size + SCALE(elements->sub_section.padding.y + 1)), ImVec2(data->rect.Min.x + rect_icon_size / 2 + SCALE(9), data->rect.Max.y + 1), true);
    draw->rect(var->gui.drawlist, data->rect.Min + ImVec2(rect_icon_size / 2, rect_icon_size + SCALE(elements->sub_section.padding.y - 1)), ImVec2(data->rect.Min.x + rect_icon_size / 2 + SCALE(10), data->rect.Max.y - SCALE(7)), draw->get_clr(clr->widgets.sub_stroke, anim->alpha), SCALE(elements->sub_section.rounding), draw_flags_round_corners_bottom_left, SCALE(1));
    draw->pop_clip_rect(var->gui.drawlist);

    if (anim->alpha >= 0.01f)
    {
        gui->set_screen_pos(data->rect.Min + ImVec2(rect_icon_size + SCALE(elements->sub_section.padding.x), SCALE(elements->sub_section.top)), pos_all);
        gui->push_var(style_var_alpha, anim->alpha * var->gui.section_alpha);
        gui->begin_content("sub_section_content_" + std::string(name), ImVec2(gui->content_avail().x, anim->anim_height), SCALE(0, 0), SCALE(0, elements->sub_section.padding.y), window_flags_no_scrollbar | window_flags_no_scroll_with_mouse);

        anim->height = gui->get_window()->ContentSize.y;

        return true;
    }

    return false;
}

void c_widgets::end_sub_section()
{
    gui->end_content();
    gui->pop_var();
}

widget_t c_widgets::sub_section_list(std::string_view name, int index, int& count)
{
    struct anim_t
    {
        ImVec4 text{ clr->widgets.text_inactive };
    };

    widget_t data = gui->register_item("sub_section_l_" + std::string(name), ImVec2(gui->content_avail().x, SCALE(12)));
    anim_t* anim = gui->anim_container<anim_t>(data->id);
    bool active = index == count;

    data->value_changed = data->hovered && gui->mouse_clicked(mouse_button_left) && gui->is_window_hovered();
    if (data->value_changed)
        count = index;

    gui->easing(anim->text, active ? clr->widgets.text.Value : clr->widgets.text_inactive.Value, 16.f, dynamic_easing);

    draw->text_clipped(var->gui.drawlist, font->get(onest_medium_data, 14), data->rect.Min - SCALE(0, 2), data->rect.Max, draw->get_clr(anim->text), name.data(), gui->text_end(name.data()), NULL, ImVec2(0.f, 0.f));

    return data;
}

widget_t c_widgets::sub_section(std::string_view name, int index, int& count, bool lua_section)
{
    struct anim_t
    {
        ImVec4 text{ clr->widgets.text_inactive };
        ImVec4 icon{ clr->widgets.text_inactive };
    };

    widget_t data = gui->register_item("sub_section_b_" + std::string(name), ImVec2(gui->content_avail().x, SCALE(12)));
    anim_t* anim = gui->anim_container<anim_t>(data->id);
    bool active = index == count;

    data->value_changed = data->hovered && gui->mouse_clicked(mouse_button_left) && gui->is_window_hovered();
    if (data->value_changed)
        count = index;

    gui->easing(anim->text, active ? clr->widgets.text.Value : clr->widgets.text_inactive.Value, 16.f, dynamic_easing);
    gui->easing(anim->icon, active ? clr->accent.Value : clr->widgets.text_inactive.Value, 16.f, dynamic_easing);

    const float rect_icon_size = gui->text_size(font->get(icons_data, 10), "L").x;
    if (!lua_section)
        draw->text_clipped(var->gui.drawlist, font->get(icons_data, 10), data->rect.Min, data->rect.Max, draw->get_clr(anim->icon), "L", NULL, NULL, ImVec2(0.f, 0.f));

    draw->text_clipped(var->gui.drawlist, font->get(onest_medium_data, 14), data->rect.Min + ImVec2(!lua_section ? rect_icon_size + SCALE(elements->sub_section.padding.x) : 0, -SCALE(2)), data->rect.Max, draw->get_clr(anim->text), name.data(), gui->text_end(name.data()), NULL, ImVec2(0.f, 0.f));

    var->gui.sub_pos = data->rect.GetBL() + SCALE(0, elements->sub_section.item_spacing);

    return data;
}