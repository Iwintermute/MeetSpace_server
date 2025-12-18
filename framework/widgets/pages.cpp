#include "../headers/includes.h"
#include "../headers/widgets.h"

void c_widgets::log_page()
{
	gui->easing(elements->log_page.offset, var->gui.login ? var->window.size.x : (var->window.size.x - elements->textfield.size.x) / 2, 10.f, dynamic_easing, true);
	if (elements->top_bar.font_size >= 60 && var->gui.login)
		var->gui.section_count = 1;
	gui->set_pos(SCALE(elements->log_page.offset, elements->log_page.top_height + elements->log_page.padding), pos_all);
	gui->begin_content("fields_content", SCALE(elements->textfield.size.x, 0), SCALE(0, 0), SCALE(0, elements->window.padding.y));
	{
		static char login[30];
		static char password[20];
		widgets->text_field("Enter username", "B", login, sizeof login);
		widgets->text_field("Enter password", "C", password, sizeof password);
		gui->set_pos((gui->window_size().x - gui->text_size(font->get(inter_medium_data, 13), "Don't have an account yet? REGISTER").x + SCALE(1)) / 2, pos_x);
		widgets->reg_button("https://discord.gg/q9mCqHsvjk");
		gui->set_pos((gui->window_size().x - SCALE(elements->button.log_size.x)) / 2, pos_x);
		if (widgets->log_button())
		{
			if(login == var->gui.username && password == var->gui.password)
				var->gui.login = true;
			if ((login != var->gui.username && password == var->gui.password) || (login == var->gui.username && password != var->gui.password))
				elements->log_notify.type = 1;
			if (login != var->gui.username && password != var->gui.password)
				elements->log_notify.type = 2;
		}
	}
	gui->end_content();
}

void c_widgets::games_page()
{
	gui->set_pos(SCALE(0, var->window.size.y - elements->window.content_offset), pos_all);
	gui->push_var(style_var_alpha, var->gui.content_alpha * elements->game_card.content_alpha);
	gui->begin_content("games_content", SCALE(0, var->window.size.y - elements->top_bar.size.y), SCALE(elements->window.padding), SCALE(elements->window.padding), window_flags_no_scrollbar);
	{
		for (int i = 0; i < elements->game_card.name.size(); i++)
		{
			gui->begin_content(elements->game_card.name.at(i) + "games_blur_main", ImVec2((gui->content_max().x - SCALE(elements->window.padding.x * 3)) / 3, SCALE(elements->card.total_height)));
			{
				if (widgets->game_card(elements->game_card.name.at(i), i, 16, 7))
				{
					var->gui.section_count = 2;
					elements->game_card.section_count = i;
				}
			}
			gui->end_content();
			if ((i + 1) % 3 != 0)
				gui->sameline();
		}
	}
	gui->end_content();
	gui->pop_var();
}

void c_widgets::product_page()
{
	
	gui->push_var(style_var_alpha, var->gui.content_alpha * elements->game_card.content_alpha);
	gui->set_pos(SCALE(0, var->window.size.y - elements->window.content_offset), pos_all);
	gui->begin_content("products_content", SCALE(0, var->window.size.y - elements->top_bar.size.y), SCALE(elements->window.padding), SCALE(elements->window.padding), window_flags_no_scrollbar);
	{
		for (int i = 0; i < elements->game_card.name.size(); ++i)
		{
			if(elements->game_card.active_section == i)
			{
				for (int j = 0; j < elements->product_card.name.at(i).size(); ++j)
				{
					gui->begin_content(elements->product_card.name.at(i).at(j) + "product_blur_main", ImVec2((gui->content_max().x - SCALE(elements->window.padding.x * 3)) / 3, SCALE(elements->card.total_height)));
					{
						if (widgets->product_card(elements->product_card.name.at(i).at(j), elements->product_card.desc.at(i).at(j), j, elements->product_card.detect.at(i).at(j), elements->product_card.rate.at(i).at(j)))
							var->gui.loading = true;
					}
					gui->end_content();
					if ((i + 1) % 3 != 0)
						gui->sameline();
				}
			}
		}
	}
	gui->end_content();
	gui->pop_var();
}