#include"../headers/includes.h"
#include "../headers/widgets.h"

bool c_widgets::game_card(std::string_view name, int img_id, int products, int undetects)
{
        struct anim_state
        {
            ImVec4 rect[3]{ clr->main.accent, clr->main.grad_1, clr->main.grad_2 };
            ImVec4 text{ clr->main.text };
            float alpha[2]{ 0 };
            bool clicked{ false };
            bool hovered{ false };
            float text_size{ 13 };
            float text_offset{ 0 };
            float rect_offset{ 0 };
        };

        ImGuiWindow* window = gui->get_window();
        if (window->SkipItems)
            return false;

        ImGuiContext& g = *GImGui;
        const ImGuiStyle& style = g.Style;
        const ImGuiID id = window->GetID(name.data());

        ImVec2 pos = window->DC.CursorPos;

        const ImRect total(pos, pos + gui->content_max());

        gui->item_size(total, style.FramePadding.y);
        if (!gui->item_add(total, id))
            return false;


        anim_state* state = gui->anim_container<anim_state>(id);

        bool held = state->hovered && gui->mouse_down(mouse_button_left);

        if ((state->hovered && gui->mouse_clicked(mouse_button_left)) || held)
            state->clicked = true;
        if (state->alpha[0] >= 0.99f && !held)
            state->clicked = false;

        gui->easing(state->alpha[0], state->clicked ? 1.f : 0.f, 4.f, static_easing);

        draw->image_rounded(window->DrawList, var->gui.games_img[img_id], total.Min, ImVec2(total.Max.x, total.Min.y + SCALE(elements->card.img_height)), ImVec2(0, 0), ImVec2(1, 1), draw->get_clr({ 1.f, 1.f, 1.f, 1.f }), SCALE(elements->card.rounding), draw_flags_round_corners_top);

        gui->set_next_window_pos(ImVec2(total.Min.x, total.Max.y - SCALE(elements->card.rect_height)), pos_all);
        gui->begin_content(std::string(name) + "games_blur", ImVec2(total.GetWidth(), SCALE(elements->card.rect_height)));
        {
            ImDrawList* drawlist = gui->window_drawlist();

            std::string prod_text = std::to_string(products);
            prod_text += " products";
            std::string und_text = std::to_string(undetects);
            und_text += " undetected";

            const ImRect rect(gui->window_pos(), gui->window_pos() + gui->window_size());
            const ImRect text_zone(rect.Min, ImVec2(rect.Max.x, rect.Min.y + SCALE(elements->window.padding.y * 3 + elements->game_card.desc_height + elements->game_card.name_height)));
            const ImRect desc_zone(ImVec2(text_zone.GetCenter().x - (gui->text_size(font->get(inter_medium_data, 12), und_text.data()).x + gui->text_size(font->get(inter_medium_data, 12), prod_text.data()).x + SCALE(elements->card.text_spacing + elements->card.radius) * 2) / 2, text_zone.Min.y + SCALE(elements->window.padding.y * 2 + elements->game_card.name_height)), ImVec2(text_zone.GetCenter().x + (gui->text_size(font->get(inter_medium_data, 12), und_text.data()).x + gui->text_size(font->get(inter_medium_data, 12), prod_text.data()).x + SCALE(elements->card.text_spacing + elements->card.radius) * 2) / 2, text_zone.Min.y + SCALE(elements->window.padding.y * 2 + elements->game_card.name_height + elements->game_card.desc_height)));
            const ImRect button_rect(ImVec2(text_zone.Min.x, text_zone.Max.y) + SCALE(elements->window.padding) - SCALE(state->rect_offset, state->rect_offset / 2), rect.Max - SCALE(elements->window.padding) + SCALE(state->rect_offset, state->rect_offset / 2));

            draw_background_blur(drawlist, var->winapi.device_dx11, var->winapi.device_context, SCALE(elements->card.rounding));
            draw->rect_filled(drawlist, rect.Min, rect.Max, draw->get_clr(clr->cards.blur_bg, 0.8), SCALE(elements->card.rounding));
            draw->text_clipped(drawlist, font->get(inter_bold_data, 13), text_zone.Min + SCALE(0, elements->window.padding.y - 3), text_zone.Max, draw->get_clr(clr->main.text), name.data(), gui->text_end(name.data()), NULL, ImVec2(0.5f, 0.f));

            draw->text_clipped(drawlist, font->get(inter_medium_data, 12), ImVec2(desc_zone.Min.x, text_zone.Min.y + SCALE(elements->window.padding.y * 2 + elements->game_card.name_height - 3)), ImVec2(desc_zone.Max.x, text_zone.Max.y), draw->get_clr(clr->main.text_inactive), prod_text.data());
            draw->circle_filled(drawlist, ImVec2(desc_zone.Min.x + gui->text_size(font->get(inter_medium_data, 12), prod_text.data()).x + SCALE(elements->card.text_spacing + elements->card.radius), desc_zone.GetCenter().y), SCALE(elements->card.radius), draw->get_clr(clr->main.text_inactive), 60);
            draw->text_clipped(drawlist, font->get(inter_medium_data, 12), ImVec2(desc_zone.Min.x, text_zone.Min.y + SCALE(elements->window.padding.y * 2 + elements->game_card.name_height - 3)), ImVec2(desc_zone.Max.x, text_zone.Max.y), draw->get_clr(clr->main.accent), und_text.data(), NULL, NULL, ImVec2(1.f, 0.f));

            state->hovered = button_rect.Contains(gui->mouse_pos());
            gui->easing(state->alpha[1], state->hovered || state->clicked ? 1.f : 0.f, 5.f, static_easing);
            gui->easing(state->text, state->hovered || state->clicked ? clr->main.black.Value : clr->main.text_inactive.Value, 12.f, dynamic_easing);
            gui->easing(state->text_size, state->hovered ? 15.f : 13.f, 16.f, dynamic_easing);
            gui->easing(state->text_offset, state->hovered ? 1.f : 0.f, 16.f, static_easing);
            gui->easing(state->rect_offset, state->clicked ? 2.f : 0.f, 14.f, dynamic_easing);
            gui->gradient("two", held, state->rect);

            draw->rect(drawlist, button_rect.Min, button_rect.Max, draw->get_clr(clr->button.card_outline), SCALE(elements->button.rounding));
            
            draw->rect_filled_multi_color(drawlist, button_rect.Min, ImVec2(button_rect.GetCenter().x, button_rect.Max.y),
            draw->get_clr(state->rect[0], state->alpha[1]),
            draw->get_clr(state->rect[1], state->alpha[1]),
            draw->get_clr(state->rect[1], state->alpha[1]),
            draw->get_clr(state->rect[0], state->alpha[1]),
            SCALE(elements->button.rounding), draw_flags_round_corners_left);

            draw->rect_filled_multi_color(drawlist, ImVec2(button_rect.GetCenter().x, button_rect.Min.y), button_rect.Max,
            draw->get_clr(state->rect[1], state->alpha[1]),
            draw->get_clr(state->rect[2], state->alpha[1]),
            draw->get_clr(state->rect[2], state->alpha[1]),
            draw->get_clr(state->rect[1], state->alpha[1]),
            SCALE(elements->button.rounding), draw_flags_round_corners_right);

            draw->text_animed(drawlist, font->get(state->hovered ? inter_bold_data : inter_medium_data, 13), SCALE(state->text_size), button_rect.Min, button_rect.Max, draw->get_clr(state->text), "View", NULL, NULL, ImVec2(0.5f, 0.5f));

        }
        gui->end_content();

    return state->alpha[0] >= 0.8f && !held;
}

bool c_widgets::product_card(std::string_view name, std::string_view desc, int img_id, int detect, float rate)
{
    struct anim_state
    {
        ImVec4 rect[3]{ clr->main.accent, clr->main.grad_1, clr->main.grad_2 };
        ImVec4 text{ clr->main.text };
        float alpha[2]{ 0 };
        bool clicked{ false };
        bool hovered{ false };
        float text_size{ 13 };
        float text_offset{ 0 };
        float rect_offset{ 0 };
    };

    ImGuiWindow* window = gui->get_window();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(name.data());

    ImVec2 pos = window->DC.CursorPos;

    const ImRect total(pos, pos + gui->content_max());

    gui->item_size(total, style.FramePadding.y);
    if (!gui->item_add(total, id))
        return false;


    anim_state* state = gui->anim_container<anim_state>(id);

    bool held = state->hovered && gui->mouse_down(mouse_button_left);

    if ((state->hovered && gui->mouse_clicked(mouse_button_left)) || held)
        state->clicked = true;
    if (state->alpha[0] >= 0.99f && !held)
        state->clicked = false;

    gui->easing(state->alpha[0], state->clicked ? 1.f : 0.f, 4.f, static_easing);

    draw->image_rounded(window->DrawList, var->gui.product_img[img_id], total.Min, ImVec2(total.Max.x, total.Min.y + SCALE(elements->card.img_height)), ImVec2(0, 0), ImVec2(1, 1), draw->get_clr({ 1.f, 1.f, 1.f, 1.f }), SCALE(elements->card.rounding), draw_flags_round_corners_top);

    gui->set_next_window_pos(ImVec2(total.Min.x, total.Max.y - SCALE(elements->card.rect_height)), pos_all);
    gui->begin_content(std::string(name) + "product_blur", ImVec2(total.GetWidth(), SCALE(elements->card.rect_height)));
    {
        ImDrawList* drawlist = gui->window_drawlist();

        std::string status = detect == 0 ? "Undetect" : detect == 1 ? "On update" : "Detect";
        std::string_view format = "%.1f";

        char rate_buf[64]; const char* rate_buf_end = gui->get_fmt(rate_buf, &rate, format);

        const ImRect rect(gui->window_pos(), gui->window_pos() + gui->window_size());
        const ImRect info_zone(rect.Min + SCALE(elements->window.padding), ImVec2(rect.Max.x, rect.Min.y) + SCALE(elements->window.padding.x, elements->window.padding.y + elements->product_card.info_zone_height));
        const ImRect img_zone(info_zone.Min, ImVec2(info_zone.Min.x + info_zone.GetHeight(), info_zone.Max.y));
        const ImRect text_zone(ImVec2(img_zone.Max.x + SCALE(elements->window.padding.x), img_zone.Min.y + SCALE(elements->product_card.text_spacing)), ImVec2(info_zone.Max.x, img_zone.Max.y - SCALE(elements->product_card.text_spacing)));
        const ImRect button_rect(ImVec2(rect.Min.x, info_zone.Max.y) + SCALE(elements->window.padding.x, elements->window.padding.y * 2)  - SCALE(state->rect_offset, state->rect_offset / 2), rect.Max - SCALE(elements->window.padding) + SCALE(state->rect_offset, state->rect_offset / 2));

        draw_background_blur(drawlist, var->winapi.device_dx11, var->winapi.device_context, SCALE(elements->card.rounding));

        draw->rect_filled(drawlist, rect.Min, rect.Max, draw->get_clr(clr->cards.blur_bg, 0.8), SCALE(elements->card.rounding));
        draw->image_rounded(drawlist, var->gui.product_img_2, img_zone.Min, img_zone.Max, ImVec2(0, 0), ImVec2(1, 1), draw->get_clr({ 1.f, 1.f, 1.f, 1.f }), SCALE(elements->product_card.rounding));

        {
            draw->text_clipped(drawlist, font->get(inter_bold_data, 13), text_zone.Min - SCALE(0, 3), text_zone.Max, draw->get_clr(clr->main.text), name.data(), gui->text_end(name.data()), NULL, ImVec2(0.f, 0.f));

            draw->text_clipped(drawlist, font->get(inter_medium_data, 12), text_zone.Min + SCALE(0, elements->card.text_spacing + elements->product_card.text_height - 2), ImVec2(text_zone.Max.x, info_zone.Max.y), draw->get_clr(detect == 0 ? clr->main.accent : detect == 1 ? clr->cards.on_update : clr->main.red), status.data());
            draw->circle_filled(drawlist, ImVec2(text_zone.Min.x + gui->text_size(font->get(inter_medium_data, 12), status.data()).x + SCALE(elements->card.text_spacing + elements->card.radius), text_zone.Max.y - SCALE(elements->product_card.text_height / 2)), SCALE(elements->card.radius), draw->get_clr(clr->main.text_inactive), 60);

            draw->text_clipped(drawlist, font->get(icons_data, 8), ImVec2(text_zone.Min.x + gui->text_size(font->get(inter_medium_data, 12), status.data()).x + SCALE(elements->card.text_spacing + elements->card.radius) * 2, text_zone.Min.y + SCALE(elements->card.text_spacing + elements->product_card.text_height)), text_zone.Max, draw->get_clr(rate >= 4.0 ? clr->main.accent : rate < 2.5 ? clr->main.red : clr->cards.on_update), "F");
            draw->text_clipped(drawlist, font->get(inter_medium_data, 12), ImVec2(text_zone.Min.x + gui->text_size(font->get(inter_medium_data, 12), status.data()).x + gui->text_size(font->get(icons_data, 8), "F").x + SCALE((elements->card.text_spacing + elements->card.radius) * 2 + elements->product_card.rate_spacing), text_zone.Min.y + SCALE(elements->card.text_spacing + elements->product_card.text_height - 2)), ImVec2(text_zone.Max.x, info_zone.Max.y), draw->get_clr(rate >= 4.0 ? clr->main.accent : rate < 2.5 ? clr->main.red : clr->cards.on_update), rate_buf, rate_buf_end, NULL, ImVec2(0.f, 0.f), NULL);
        }

        state->hovered = button_rect.Contains(gui->mouse_pos()) && gui->is_window_hovered(NULL);
        gui->easing(state->alpha[1], state->hovered || state->clicked ? 1.f : 0.f, 5.f, static_easing);
        gui->easing(state->text, state->hovered || state->clicked ? clr->main.black.Value : clr->main.text_inactive.Value, 12.f, dynamic_easing);
        gui->easing(state->text_size, state->hovered ? 15.f : 13.f, 16.f, dynamic_easing);
        gui->easing(state->text_offset, state->hovered ? 1.f : 0.f, 16.f, static_easing);
        gui->easing(state->rect_offset, state->clicked ? 2.f : 0.f, 14.f, dynamic_easing);
        gui->gradient("three", held, state->rect);

        draw->rect(drawlist, button_rect.Min, button_rect.Max, draw->get_clr(clr->button.card_outline), SCALE(elements->button.rounding));

        draw->rect_filled_multi_color(drawlist, button_rect.Min, ImVec2(button_rect.GetCenter().x, button_rect.Max.y),
            draw->get_clr(state->rect[0], state->alpha[1]),
            draw->get_clr(state->rect[1], state->alpha[1]),
            draw->get_clr(state->rect[1], state->alpha[1]),
            draw->get_clr(state->rect[0], state->alpha[1]),
            SCALE(elements->button.rounding), draw_flags_round_corners_left);

        draw->rect_filled_multi_color(drawlist, ImVec2(button_rect.GetCenter().x, button_rect.Min.y), button_rect.Max,
            draw->get_clr(state->rect[1], state->alpha[1]),
            draw->get_clr(state->rect[2], state->alpha[1]),
            draw->get_clr(state->rect[2], state->alpha[1]),
            draw->get_clr(state->rect[1], state->alpha[1]),
            SCALE(elements->button.rounding), draw_flags_round_corners_right);

        draw->text_animed(drawlist, font->get(state->hovered ? inter_bold_data : inter_medium_data, 13.f), SCALE(state->text_size), button_rect.Min - SCALE(0, state->text_offset), button_rect.Max, draw->get_clr(state->text), "Start", NULL, NULL, ImVec2(0.5f, 0.5f));

    }
    gui->end_content();

    return state->alpha[0] >= 0.8f && !held;
}
