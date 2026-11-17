#include "../headers/functions.h"
#include "../headers/widgets.h"

widget_t c_widgets::button(std::string_view name)
{
    struct anim_t
    {
        float active{ false };
        float alpha{ 0 };
        ImVec4 text{ clr->widgets.text_inactive };
    };

    widget_t data = gui->register_item(name, ImVec2(gui->content_avail().x, SCALE(elements->button.height)));
    anim_t* anim = gui->anim_container<anim_t>(data->id);

    data->value_changed = data->hovered && gui->mouse_clicked(mouse_button_left) && gui->is_window_hovered();

    if (data->value_changed || data->hovered && gui->mouse_down(mouse_button_left) && gui->is_window_hovered())
        anim->active = true;

    gui->easing(anim->alpha, (data->hovered && gui->is_window_hovered()) && !anim->active ? 1.f : 0.f, 8.f, static_easing);
    gui->easing(anim->text, (data->hovered&& gui->is_window_hovered()) && !anim->active ? clr->widgets.text.Value : clr->widgets.text_inactive.Value, 16.f, dynamic_easing);

    if (anim->alpha <= 0.01f)
        anim->active = false;

    draw->rect_filled(gui->window_drawlist(), data->rect.Min, data->rect.Max, draw->get_clr(clr->widgets.background), SCALE(elements->button.rounding));
    draw->rect_filled(gui->window_drawlist(), data->rect.Min, data->rect.Max, draw->get_clr(clr->widgets.sub_stroke, 0.3f * anim->alpha), SCALE(elements->button.rounding));
    draw->rect(gui->window_drawlist(), data->rect.Min, data->rect.Max, draw->get_clr(clr->widgets.stroke), SCALE(elements->button.rounding), 0, SCALE(1));
    draw->text_clipped(gui->window_drawlist(), font->get(onest_medium_data, 14), data->rect.Min, data->rect.Max, draw->get_clr(anim->text), name.data(), gui->text_end(name.data()), NULL, ImVec2(0.5f, 0.5f));

    return data;
}

widget_t c_widgets::text_box(std::string_view name, std::string_view text)
{
    ImFont* text_font = font->get(onest_medium_data, 14);
    const float pad = SCALE(12.f);
    const float width = gui->content_avail().x;
    const float wrap = ImMax(1.f, width - pad * 2.f);
    const char* begin = text.data();
    const char* end = begin + text.size();
    ImVec2 text_size = gui->text_size(text_font, begin, end, false, wrap);
    widget_t data = gui->register_item(name, ImVec2(width, text_size.y + pad * 2.f));

    draw->rect_filled(gui->window_drawlist(), data->rect.Min, data->rect.Max, draw->get_clr(clr->widgets.background), SCALE(elements->button.rounding));
    draw->rect(gui->window_drawlist(), data->rect.Min, data->rect.Max, draw->get_clr(clr->widgets.stroke), SCALE(elements->button.rounding), 0, SCALE(1));
    draw->text(gui->window_drawlist(), text_font, text_font ? text_font->FontSize : 14.f, data->rect.Min + ImVec2(pad, pad), draw->get_clr(clr->widgets.text), begin, end, wrap);
    return data;
}