#include "headers/includes.h"
#include "headers/widgets.h"
#include "headers/app_state.h"
#include "headers/chat_manager.h"
#include "headers/messenger_widgets.h"
#include "headers/conference_widgets.h"

void c_gui::render()
{
	gui->initialize();

	gui->set_next_window_pos(ImVec2(0, 0));
	gui->set_next_window_size(SCALE(var->window.size));
	gui->begin("messenger", nullptr, var->window.flags);
	{
		gui->set_style();
		
		// Рендерим соответствующий экран в зависимости от состояния
		switch (app_state->current_screen)
		{
		case AppScreen::Login:
			messenger_widgets->render_login_screen();
			break;

		case AppScreen::Registration:
			messenger_widgets->render_registration_screen();
			break;

		case AppScreen::PhoneVerification:
			messenger_widgets->render_phone_verification_screen();
			break;

		case AppScreen::MainMessenger:
			messenger_widgets->render_main_messenger();
			break;

		case AppScreen::Profile:
			messenger_widgets->render_profile_screen();
			break;

		case AppScreen::Settings:
			// TODO: Implement settings screen
			break;

		case AppScreen::Conference:
			if (conference_widgets)
				conference_widgets->render_conference_ui();
			else
				draw->text_clipped(gui->get_window()->DrawList, font->get(inter_bold_data, 20), 
                     ImVec2(100, 100), ImVec2(400, 200), IM_COL32(255, 0, 0, 255), 
                     "Error: Conf Widgets NULL", nullptr, nullptr, ImVec2(0,0));
			break;

		default:
			draw->text_clipped(gui->get_window()->DrawList, font->get(inter_bold_data, 20), 
                     ImVec2(100, 100), ImVec2(400, 200), IM_COL32(255, 0, 0, 255), 
                     "Error: Unknown Screen Switch", nullptr, nullptr, ImVec2(0,0));
			break;
		}

		// Обработка горячих клавиш
		if (IsKeyDown(ImGuiKey_LeftAlt))
		{
			if (IsKeyReleased(ImGuiKey_Minus))
			{
				var->gui.stored_dpi -= 10;
				var->gui.dpi_changed = true;
				var->gui.update_window = true;
			}

			if (IsKeyReleased(ImGuiKey_Equal))
			{
				var->gui.stored_dpi += 10;
				var->gui.dpi_changed = true;
				var->gui.update_window = true;
			}

			var->gui.stored_dpi = ImClamp(var->gui.stored_dpi, 70, 200);
		}

		// Обработка перемещения окна
		gui->move_window(var->winapi.hwnd, var->winapi.rc);
	}
	gui->end();
}