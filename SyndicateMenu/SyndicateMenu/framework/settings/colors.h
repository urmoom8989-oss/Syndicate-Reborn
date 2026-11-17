#pragma once
#include "imgui.h"
#include <memory>

class c_colors
{
public:
    ImColor accent{ 111, 119, 255 };

    struct
    {
        ImColor background{ 16, 16, 21 };
        ImColor sub_background{ 18, 18, 23 };
        ImColor stroke{ 25, 25, 34 };
        ImColor section{ 23, 23, 30 };
    } window;

    struct
    {
        ImColor background{ 21, 21, 28 };
        ImColor sub_background{ 37, 37, 57 };
        ImColor text{ 255, 255, 255 };
        ImColor text_inactive{ 79, 79, 98 };
        ImColor stroke{ 25, 25, 34 };
        ImColor sub_stroke{ 34, 34, 48 };
    } widgets;
};

inline std::unique_ptr<c_colors> clr = std::make_unique<c_colors>();
