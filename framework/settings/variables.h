#pragma once
#include <string>
#include <vector>
#include "imgui.h"
#include "../headers/flags.h"
#include <wtypes.h>
#include <d3d11.h>

struct ID3D11ShaderResourceView;
struct ID3D11Device;

class c_variables
{
public:
	struct
	{
		HWND hwnd;
		RECT rc;
		ID3D11DeviceContext* device_context{ nullptr };
		IDXGISwapChain* swap_chain{ nullptr };
		ID3D11Device* device_dx11{ nullptr };
	} winapi;

	struct
	{
		ImVec2 size{ 900, 530 };
		float rounding{ 12 };
		window_flags flags{ window_flags_no_saved_settings | window_flags_no_nav | window_flags_no_decoration | window_flags_no_scrollbar | window_flags_no_scroll_with_mouse | window_flags_no_background };
		ImVec2 padding{ 0, 0 };
		ImVec2 spacing{ 0, 0 };
		float shadow{ 0 };
		float border{ 0 };
	} window;

	struct
	{
		float dpi{ 1.f };
		int stored_dpi{ 100 };
		int anim_dpi{ 100 };
		bool update_window{ false };
		bool dragging{ false };
		bool dpi_changed{ false };
		bool login{ false };
		float content_alpha{ 0.f };
		int active_section{ 0 };
		int section_count{ 0 };
		float windows_alpha{ 0 };
		bool loading = false;

		std::string username{ "123" };
		std::string password{ "123" };

		ID3D11ShaderResourceView* logo_back;
		ID3D11ShaderResourceView* games_img[6];
		ID3D11ShaderResourceView* product_img[3];
		ID3D11ShaderResourceView* product_img_2;

	} gui;

	gui_style style;

};

inline std::unique_ptr<c_variables> var = std::make_unique<c_variables>();
