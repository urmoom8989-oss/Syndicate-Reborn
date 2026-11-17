#include "../headers/functions.h"
#include "../headers/widgets.h"

void c_widgets::begin_child(std::string_view name, bool full_width)
{
    struct child_state
    {
        float height{ 0 };
        float width{ 0 };
    };

    ImGuiWindow* window = gui->get_window();
    const ImVec2 pos = window->DC.CursorPos;

    child_state* anim = gui->anim_container<child_state>(window->GetID(name.data()));
    const float half_width = (gui->content_max().x - var->style.item_spacing.x - var->style.window_padding.x) / 2.f;
    anim->width = full_width ? (gui->content_max().x - var->style.window_padding.x) : half_width;

    draw->rect_filled(gui->window_drawlist(), pos, pos + ImVec2(anim->width, anim->height), draw->get_clr(clr->window.sub_background), SCALE(elements->child.rounding));
    draw->rect(gui->window_drawlist(), pos, pos + ImVec2(anim->width, anim->height), draw->get_clr(clr->window.stroke), SCALE(elements->child.rounding), 0, SCALE(1));

    gui->push_var(style_var_window_padding, SCALE(elements->child.padding));
    gui->begin_def_child(name.data() + std::string("content_child"), ImVec2(anim->width, anim->height), 0, window_flags_always_use_window_padding | window_flags_no_move | window_flags_nav_flattened | window_flags_no_saved_settings | window_flags_no_scrollbar | window_flags_no_scroll_with_mouse);
    gui->push_var(style_var_item_spacing, SCALE(elements->child.spacing));

    anim->height = gui->get_window()->ContentSize.y + SCALE(elements->child.padding.y * 2);
}

void c_widgets::end_child()
{
    gui->pop_var();
    gui->end_def_child();
    gui->pop_var();
}