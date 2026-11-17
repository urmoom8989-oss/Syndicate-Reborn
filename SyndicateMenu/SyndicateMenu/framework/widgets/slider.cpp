#include "../headers/functions.h"
#include "../headers/widgets.h"

template <typename T>
widget_t slider_ex(std::string_view name, T* callback, T min, T max, std::string_view format)
{
    struct anim_t
    {
        float offset{ 0 };
        T slow_value = 0;
    };

    widget_t data = gui->register_item(name, ImVec2(gui->content_avail().x, SCALE(elements->slider.height)));
    anim_t* anim = gui->anim_container<anim_t>(data->id);
    const ImRect rect{ ImVec2(data->rect.Min.x, data->rect.Max.y - SCALE(elements->slider.size)), data->rect.Max };

    bool hovered, held;
    bool pressed = gui->button_behavior(rect, data->id, &hovered, &held);

    const float padding = SCALE(0);
    const float grab_width = SCALE(elements->slider.size);
    const float width_with_grab = rect.GetWidth() - grab_width - padding * 2;
    const float grab_offset = padding + grab_width / 2 + static_cast<float>((*callback - min)) / static_cast<float>(max - min) * width_with_grab;

    char value_buf[64]; const char* value_buf_end = gui->get_fmt(value_buf, callback, format);
    std::string current_str(value_buf, value_buf_end - value_buf);

    const float t = ImSaturate((gui->mouse_pos().x - (rect.Min.x + padding + grab_width / 2)) / width_with_grab);
    data->value_changed = held;

    if (data->value_changed)
    {
        T new_value = static_cast<T>(min + t * (max - min));
        *callback = ImClamp(new_value, min, max);
    }

    gui->easing(anim->offset, grab_offset, 20.f, dynamic_easing);

    draw->rect_filled(gui->window_drawlist(), rect.Min + SCALE(0, elements->slider.padding), rect.Max - SCALE(0, elements->slider.padding), draw->get_clr(clr->widgets.background), rect.GetHeight() / 2);
    draw->rect(gui->window_drawlist(), rect.Min + SCALE(0, elements->slider.padding), rect.Max - SCALE(0, elements->slider.padding), draw->get_clr(clr->widgets.stroke), rect.GetHeight() / 2, 0, SCALE(1));

    float h, s, v;
    ImVec4 color{ 1.f, 1.f, 1.f, 1.f };
    gui->rgb_to_hsv(clr->accent.Value.x, clr->accent.Value.y, clr->accent.Value.z, h, s, v);

    v = std::max(0.0f, v - 0.6f);
    gui->hsv_to_rgb(h, s, v, color.x, color.y, color.z);

    draw->rect_filled_multi_color(gui->window_drawlist(), rect.Min + SCALE(0, elements->slider.padding), ImVec2(rect.Min.x + anim->offset, rect.Max.y - SCALE(elements->slider.padding)), draw->get_clr(clr->accent), draw->get_clr(color, 1.f), draw->get_clr(color, 1.f), draw->get_clr(clr->accent), rect.GetHeight() / 2);
    draw->circle_filled(gui->window_drawlist(), ImVec2(rect.Min.x + anim->offset, rect.GetCenter().y), SCALE(elements->slider.size / 2), draw->get_clr(clr->widgets.text), 30);

    T* data_ptr = static_cast<T*>(callback);
    T target_value = *data_ptr;

    if constexpr (std::is_arithmetic_v<T>) {

        T step = (target_value - anim->slow_value) * gui->fixed_speed(15.f);
        if (step == 0 && anim->slow_value != target_value) step = (target_value > anim->slow_value) ? 1 : -1;
        anim->slow_value += step;
    }

    snprintf(value_buf, sizeof(value_buf), format.data(), anim->slow_value);

    draw->text_clipped(gui->window_drawlist(), font->get(onest_medium_data, 14), data->rect.Min - SCALE(0, 3), rect.GetTR(), draw->get_clr(clr->widgets.text_inactive), name.data(), gui->text_end(name.data()), NULL, ImVec2(0.f, 0.f));
    draw->text_clipped(gui->window_drawlist(), font->get(onest_medium_data, 14), data->rect.Min - SCALE(0, 3), rect.GetTR(), draw->get_clr(clr->widgets.text), value_buf, NULL, NULL, ImVec2(1.f, 0.f));

    if (var->gui.section_alpha == 0.f || var->gui.sub_section_alpha == 0.f)
    {
        anim->offset = 0.f;
    }


    return data;
}

widget_t c_widgets::slider_int(std::string_view key)
{
    slider_int_t* data = cfg->fill<slider_int_t>(key.data());

    return slider_ex(data->name, &data->callback, data->min, data->max, data->format);
}

widget_t c_widgets::slider_float(std::string_view key)
{
    slider_float_t* data = cfg->fill<slider_float_t>(key.data());

    return slider_ex(data->name, &data->callback, data->min, data->max, data->format);
}