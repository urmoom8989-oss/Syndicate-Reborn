#pragma once
#include "includes.h"
#include "../headers/config.h"

#define IMGUI_DEFINE_MATH_OPERATORS

struct widget_t
{
    ImGuiID id;
    bool value_changed;
    ImRect rect;
    ImVec2 size;
    bool visible;
    bool hovered;

    widget_t* operator->() {
        return this;
    }

    const widget_t* operator->() const {
        return this;
    }
};

class c_widgets
{
public:

    widget_t section(std::string_view icon, int index, int& count);

    void begin_section(std::string_view name);

    void end_section();

    bool begin_sub_section(std::string_view name);

    void end_sub_section();

    widget_t sub_section_list(std::string_view name, int index, int& count);

    widget_t sub_section(std::string_view name, int index, int& count, bool lua_section = false);

    void begin_child(std::string_view name, bool full_width = false);

    void end_child();

    widget_t checkbox(std::string_view key);

    widget_t slider_int(std::string_view key);

    widget_t slider_float(std::string_view key);

    widget_t range_int(std::string_view key);

    widget_t range_float(std::string_view key);

    widget_t dropdown(std::string_view key);

    widget_t multi_dropdown(std::string_view key);

    widget_t button(std::string_view name);

    widget_t text_box(std::string_view name, std::string_view text);

    widget_t key_select(std::string_view name, int* key);

    widget_t mode_select(std::string_view name, int* mode);

    bool color_edit(std::string_view key);

    widget_t picker_field(std::string_view name, char* buf, int size, float width);

    widget_t hex_field(std::string_view name, char* buf, int size, float width);

    widget_t search_field(std::string_view name, char* buf, int size, float width);
};

inline std::unique_ptr<c_widgets> widgets = std::make_unique<c_widgets>();

enum notify_type
{
    success = 0,
    warning = 1,
    error = 2
};

struct notify_state
{
    int notify_id;
    std::string_view text;
    notify_type type{ success };

    ImVec2 window_size{ 0, 0 };
    float notify_alpha{ 0 };
    bool active_notify{ true };
    float notify_timer{ 0 };
    float notify_pos{ 0 };
};

class c_notify
{
public:
    void setup_notify();

    void add_notify(std::string_view text, notify_type type);

private:
    ImVec2 render_notify(int cur_notify_value, float notify_alpha, float notify_percentage, float notify_pos, std::string_view text, notify_type type);

    float notify_time{ 15 };
    int notify_count{ 0 };

    float notify_spacing{ 20 };
    ImVec2 notify_padding{ 20, 20 };

    std::vector<notify_state> notifications;

};

inline std::unique_ptr<c_notify> notify = std::make_unique<c_notify>();
