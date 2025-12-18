#pragma once
#include <string>
#include "imgui.h"

class c_elements
{
public:

	struct
	{
		ImVec2 padding{ 15, 15 };
		float content_offset{ 0 };
		float main_content_padding{ 170 };
	} window;

	struct
	{
		ImVec2 rect_size{ 90, 90 };

		ImVec2 size{ 80, 80 };
		ImVec2 def_size{ 80, 80 };
		float pos_y{ 125 };
		float def_rounding{ 8 };
		float round_top{ 8 };
		float round_bottom{ 12 };
		float font_size{ 40 };
		float product_height{ 100 };

		float log_line_width{ 0 };
		float log_line_alpha{ 0 };

		float product_line_def_width{ 220 };
		float product_line_width{ 0 };
		float product_line_padding{ 10 };

		float product_outline_alpha{ 0 };
	} top_bar;

	struct
	{
		float top_height{ 170 };
		float padding{ 78 };
		float offset{ 0 };
	} log_page;

	struct
	{
		ImVec2 size{ 320, 50 };
		ImVec2 icon_zone_size{ 40, 40 };
		ImVec2 icon_padding{ 5, 5 };
		float text_spacing{ 10 };
		float rounding{ 6 };
	} textfield;

	struct
	{
		float rounding{ 6 };

		ImVec2 close_size{ 15, 15 };
		float close_padding{ 25 };
		float close_pos{ 25 };

		ImVec2 log_size{ 256, 40 };

		float reg_height{ 13 };
		float reg_radius{ 1 };
		float reg_circ_count{ 15 };
		
		ImVec2 return_size{ 20, 20 };
		float return_pos{ 40 };
		float return_def_pos{ 40 };
	} button;

	struct
	{
		float total_height{ 260 };
		float img_height{ 158 };
		float rect_height{ 130 };
		float text_spacing{ 10 };
		float rounding{ 8 };
		float radius{ 2 };
	} card;

	struct
	{
		std::vector<std::string> name{ "Escape from tarkov", "Arena Breakout: Infinite", "RUST", "Dead by daylight", "Dayz", "Apex legends" };
		float content_alpha{ 0.f };
		int active_section{ -1 };
		int section_count{ -1 };

		float name_height{ 8 };
		float desc_height{ 7 };
	} game_card;

	struct
	{
		std::vector<std::vector<std::string>> name{ {"Dopamine", "ThunderX", "Quantum Cheats"}, {}, {}, {}, {}, {} };
		std::vector<std::vector<std::string>> desc{ {"TOP PRODUCT", " ", " "}, {}, {}, {}, {}, {}};
		std::vector<std::vector<int>> detect{ {0, 1, 2}, {}, {}, {}, {}, {} };
		std::vector<std::vector<float>> rate{ {5.0, 1.0, 3.0}, {}, {}, {}, {}, {} };

		float info_zone_height{ 30 };
		float rounding{ 4 };
		float rate_spacing{ 7 };
		float text_height{ 8 };
		float text_spacing{ 2 };
	} product_card;

	struct
	{
		ImVec2 size{ 520, 260 };
		float text_spacing{ 20 };
		float text_padding{ 82 };
		float top_height{ 88 };
		float top_padding{ 22 };
		float logo_height{ 25 };
		float offset{ 0 };
		float window_rounding{ 8 };
		float message_rounding{ 6 };
		int type{ 0 };
		float text_width{ 313 };
	} log_notify;

	struct
	{
		ImVec2 size{ 520, 143 };
		float offset{ 0 };
		float button_height{ 40 };
		float line_height{ 10 };
	} loading;
};

inline std::unique_ptr<c_elements> elements = std::make_unique<c_elements>();
