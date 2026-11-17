#pragma once
#include <string>
#include <vector>
#include "imgui.h"
#include "../headers/flags.h"
#include <map>

enum class section_id
{
	general,
	wallhack,
	world,
	movement,
	weapon,
	customize,
	stream,
	misc
};

struct sub_section
{
	std::string name;
	int index;
	bool is_group;
	std::vector<sub_section> items;
	bool is_external;
};

struct section
{
	std::string icon;
	std::string name;
	bool separator;
	std::vector<sub_section> subsections;
};

class c_variables
{
public:
	struct
	{
		std::string name{ "syndicate" };
		window_flags flags{ window_flags_no_saved_settings | window_flags_no_decoration | window_flags_no_scroll_with_mouse | window_flags_no_scrollbar | window_flags_no_background | window_flags_no_nav };
		ImVec2 size{ 860, 492 };
		ImVec2 padding{ 0, 0 };
		ImVec2 spacing{ 0, 0 };
		float rounding{ 8 };
		float shadow{ 0 };
		float border{ 0 };
		float scrollbar{ 10 };
		float scrollbar_content{ 0 };
		ImVec2 scrollbar_border{ 12, 12 };
		float scrollbar_offset{ 20 };
	} window;

	struct
	{
		float bar_width{ 60 };
		float sidebar_width{ 200 };

		std::pair<section_id, std::vector<section>> sections_data
		{
			section_id::general,
			{
				{
					"B", "Aim", false,
					{
						{"Aimbot", 0, false, {}}
					}
				},
				{
					"C", "Visuals", false,
					{
						{"ESP", 0, false, {}},
						{"Customize", 1, false, {}}
					}
				},
				{
					"E", "World", false,
					{
						{"Loot", 0, false, {}}
					}
				},
				{
					"G", "Movement", false,
					{
						{"Move", 0, false, {}}
					}
				},
				{
					"H", "Weapon", false,
					{
						{"Gun", 0, false, {}}
					}
				},
				{
					"D", "Themes", false,
					{
						{"Themes", 0, false, {}}
					}
				},
				{
					"C", "Stream", false,
					{
						{"Stream proof", 0, false, {}}
					}
				},
				{
					"F", "Settings", false,
					{
						{"Credits", 0, false, {}},
						{"Settings", 1, false, {}}
					}
				},
			}
		};
		section_id active_section{ section_id::general };
		std::vector<int> count_subsections{ 0, 0, 0, 0, 0, 0, 0, 0 };
		std::vector<int> active_subsections{ 0, 0, 0, 0, 0, 0, 0, 0 };
		float section_alpha{ 1 };
		float sub_section_alpha{ 1 };

		float dpi{ 1 };
		int stored_dpi{ 100 };
		bool dpi_changed{ true };
		bool dpi_fix{ false };
		float anim_speed{ 1 };

		ImRect color_edit_rect;
		ImVec2 sub_pos{};
		ImDrawList* drawlist;

		std::string name{ "Syndicate Menu" };
		std::string date{ "INSERT menu  |  END unload" };
		ImTextureID texture{ nullptr };

		float section_pos{ 0 };
	} gui;

	gui_style style;
	bool request_unload = false;

};

inline std::unique_ptr<c_variables> var = std::make_unique<c_variables>();
