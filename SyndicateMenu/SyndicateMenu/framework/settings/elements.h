#pragma once
#include <string>
#include "imgui.h"

class c_elements
{
public:

    struct
    {
        float window_padding{ 15 };
        float line_padding{ 25 };
        float text_padding{ 10 };
        float rounding{ 4 };
        float image_size{ 30 };
    } topbar;

    struct
    {
        ImVec2 padding{ 10, 10 };
        ImVec2 spacing{ 10, 10 };
        float rounding{ 6 };
    } section;

    struct
    {
        ImVec2 padding{ 15, 15 };
        ImVec2 spacing{ 15, 20 };
        float rounding{ 4 };
        float top{ 25 };
        float bottom{ 25 };
        float item_spacing{ 17 };
    } sub_section;

    struct
    {
        ImVec2 padding{ 12, 12 };
        ImVec2 spacing{ 12, 12 };
    } content;

    struct
    {
        ImVec2 padding{ 15, 15 };
        ImVec2 spacing{ 15, 15 };
        float rounding{ 4 };
    } child;

    struct
    {
        float height{ 15 };
        float size{ 25 };
        float radius{ 4.5 };
        float padding{ 3 };
        float add_size{ 15 };
    } checkbox;

    struct
    {
        float height{ 25 };
        float size{ 9 };
        float padding{ 1 };
    } slider;

    struct
    {
        float height{ 48 };
        float size{ 30 };
        float padding{ 10 };
        float rounding{ 4 };
        float window_padding{ 10 };
        float scrollbar_content{ 8 };
        ImVec2 scrollbar_border{ 8, 8 };
    } dropdown;

    struct
    {
        float height{ 30 };
        float padding{ 10 };
    } selectable;

    struct
    {
        float height{ 30 };
        float rounding{ 4 };
    } button;

    struct
    {
        float height{ 30 };
        float rounding{ 4 };
        float width{ 170 };
        float window_padding{ 10 };
        ImVec2 padding{ 10, 10 };
        ImVec2 spacing{ 10, 10 };
    } keybind;

    struct
    {
        float width{ 220 };
        float title{ 35 };
        float rounding{ 6 };
        float content_rounding{ 4 };
        float circle_size{ 10 };
        ImVec2 padding{ 10, 10 };
        ImVec2 spacing{ 10, 10 };
        ImVec2 bar_size{ 140, 8 };
        float grab_width{ 4 };
        float grab_padding{ 1 };
        float button_height{ 30 };
        float window_padding{ 10 };
    } color_edit;

    struct
    {
        float height{ 52 };
        float size{ 30 };
        float rounding{ 2 };
        float padding{ 12 };
        float line_padding{ 7 };
        float search_rounding{ 4 };
    } text_field;
};

inline std::unique_ptr<c_elements> elements = std::make_unique<c_elements>();
