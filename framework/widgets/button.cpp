#include"../headers/includes.h"
#include "../headers/widgets.h"

bool c_widgets::close_button()
{
    struct anim_state
    {
        ImVec4 text{ clr->main.text };
        float alpha{ 0 };
        bool clicked{ false };
        float text_size{ 8 };
    };

    ImGuiWindow* window = gui->get_window();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID("close_button");

    ImVec2 pos = window->DC.CursorPos;

    const ImRect total(pos, pos + SCALE(elements->button.close_size));

    gui->item_size(total, style.FramePadding.y);
    if (!gui->item_add(total, id))
        return false;

    bool hovered, held;
    bool pressed = gui->button_behavior(total, id, &hovered, &held, NULL);

    anim_state* state = gui->anim_container<anim_state>(id);

    if (pressed)
        state->clicked = true;
    if (state->alpha >= 0.99f)
        SendMessage(var->winapi.hwnd, WM_CLOSE, 0, 0);

    gui->easing(state->text, state->clicked || hovered ? clr->main.accent.Value : (elements->button.close_pos >= SCALE(elements->top_bar.product_height - elements->button.close_size.y) / 2 - 1 ? clr->main.text_inactive.Value : clr->main.text.Value), 12.f, dynamic_easing);
    gui->easing(state->alpha, state->clicked ? 1.f : 0.f, 5.f, static_easing);
    gui->easing(state->text_size, state->clicked || hovered ? 14.f : elements->button.close_pos >= SCALE(elements->top_bar.product_height - elements->button.close_size.y) / 2 - 1 ? 10.f : 8.f, 40.f, static_easing);

    draw->rotate_start(window->DrawList);
    draw->text_animed(window->DrawList, font->get(icons_data, 8), SCALE(state->text_size), total.Min, total.Max, draw->get_clr(state->text), "D", NULL, NULL, ImVec2(0.5f, 0.5f));
    draw->rotate_end(window->DrawList, 360 * state->alpha, total.GetCenter());

    return pressed;
}

bool c_widgets::return_button()
{
    struct anim_state
    {
        ImVec4 text{ clr->main.text_inactive };
        float alpha{ 0 };
        bool clicked{ false };
        float text_offset{ SCALE(4) };
    };

    ImGuiWindow* window = gui->get_window();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    std::string ids = "return_button";
    ids += std::to_string(elements->game_card.active_section);
    const ImGuiID id = window->GetID(ids.data());

    ImVec2 pos = window->DC.CursorPos;

    anim_state* state = gui->anim_container<anim_state>(id);

    const ImRect total(pos, pos + SCALE(elements->button.return_size));

    gui->item_size(total, style.FramePadding.y);
    if (!gui->item_add(total, id))
        return false;

    bool hovered = total.Contains(gui->mouse_pos());
    bool pressed = hovered && gui->mouse_clicked(mouse_button_left);

    if (pressed)
    {
        state->clicked = true;
        var->gui.section_count = 1;
        elements->game_card.section_count = -1;
    }
    if (state->alpha >= 0.99)
        state->clicked = false;

    gui->easing(state->text, state->clicked || hovered ? clr->main.text.Value : clr->main.text_inactive.Value, 12.f, dynamic_easing);
    gui->easing(state->alpha, state->clicked ? 1.f : 0.f, 5.f, static_easing);
    gui->easing(state->text_offset, hovered ?  0.f : SCALE(4.f), 12.f, dynamic_easing);

    draw->text_clipped(window->DrawList, font->get(icons_data, 12), total.Min, total.Max - ImVec2(state->text_offset, 0), draw->get_clr(state->text), "Z", NULL, NULL, ImVec2(1.f, 0.5f)); // Z=Navigation/Return

    return pressed;
}

bool c_widgets::log_button()
{
    struct anim_state
    {
        ImVec4 text{ clr->main.text };
        ImVec4 rect[3]{ clr->main.accent };
        float offset{ 0 };
        float alpha[2]{ 0 };
        bool clicked{ false };
    };

    ImGuiWindow* window = gui->get_window();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID("log_button");

    ImVec2 pos = window->DC.CursorPos;

    anim_state* state = gui->anim_container<anim_state>(id);

    const ImRect total(pos, pos + SCALE(elements->button.log_size));
    const ImRect rect(total.Min + ImVec2(state->offset, state->offset / 2), total.Max - ImVec2(state->offset, state->offset / 2));
    gui->item_size(total, style.FramePadding.y);
    if (!gui->item_add(total, id))
        return false;

    bool hovered, held;
    bool pressed = gui->button_behavior(total, id, &hovered, &held, NULL);

    if (pressed || held)
        state->clicked = true;
    if (state->alpha[0] >= 0.99f && !held)
        state->clicked = false;

    gui->easing(state->alpha[1], hovered ? 1.f : 0.f, 5.f, static_easing);
    gui->easing(state->text, hovered ? clr->main.black.Value : clr->main.text_inactive.Value, 12.f, dynamic_easing);
    gui->easing(state->alpha[0], state->clicked ? 1.f : 0.f, 5.f, static_easing);
    gui->easing(state->offset, state->clicked ? SCALE(6) : 0.f, 10.f, dynamic_easing);

    gui->gradient("one", held, state->rect);

    draw->rect(window->DrawList, rect.Min, rect.Max, draw->get_clr(clr->button.log_outline), SCALE(elements->button.rounding));
    draw->rect_filled_multi_color(window->DrawList, rect.Min, ImVec2(rect.GetCenter().x, rect.Max.y), draw->get_clr(state->rect[0], state->alpha[1]), draw->get_clr(state->rect[1], state->alpha[1]), draw->get_clr(state->rect[1], state->alpha[1]), draw->get_clr(state->rect[0], state->alpha[1]), SCALE(elements->button.rounding), draw_flags_round_corners_left);
    draw->rect_filled_multi_color(window->DrawList, ImVec2(rect.GetCenter().x, rect.Min.y), rect.Max, draw->get_clr(state->rect[1], state->alpha[1]), draw->get_clr(state->rect[2], state->alpha[1]), draw->get_clr(state->rect[2], state->alpha[1]), draw->get_clr(state->rect[1], state->alpha[1]), SCALE(elements->button.rounding), draw_flags_round_corners_right);

    draw->text_clipped(window->DrawList, font->get(inter_bold_data, 13), total.Min, total.Max, draw->get_clr(state->text), "LOG IN", NULL, NULL, ImVec2(0.5f, 0.5f));

    return state->alpha[0] >= 0.8f && !held;
}

bool c_widgets::reg_button(std::string_view url)
{
    struct anim_state
    {
        ImVec4 text{ clr->main.text };
        ImVec4 rect[3]{ clr->main.accent };
        float alpha[15]{ 1 };
        bool clicked{ false };
    };

    ImGuiWindow* window = gui->get_window();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID("reg_button");

    ImVec2 pos = window->DC.CursorPos;

    anim_state* state = gui->anim_container<anim_state>(id);

    const ImRect total(pos, pos + ImVec2(gui->text_size(font->get(inter_medium_data, 13), "Don't have an account yet? REGISTER").x + SCALE(1), SCALE(elements->button.reg_height)));
    const ImRect button_rect(ImVec2(total.Max.x - gui->text_size(font->get(inter_medium_data, 13), "REGISTER").x, total.Min.y), total.Max);
    float circ_padding = (button_rect.GetWidth() - SCALE(elements->button.reg_radius * 2) * elements->button.reg_circ_count) / (elements->button.reg_circ_count - 1);
    gui->item_size(total, style.FramePadding.y);
    if (!gui->item_add(total, id))
        return false;

    bool hovered, held;
    bool pressed = gui->button_behavior(button_rect, id, &hovered, &held, NULL);

    if (pressed) 
        state->clicked = true;
    if (state->clicked && state->alpha[14] <= 0.01)
    {
        state->clicked = false;
        gui->open_url(url.data());
    }

    gui->easing(state->rect[0], hovered ? clr->main.accent.Value : clr->main.accent.Value, 12.f, dynamic_easing);
    gui->easing(state->rect[1], hovered ? clr->main.grad_1.Value : clr->main.accent.Value, 12.f, dynamic_easing);
    gui->easing(state->rect[2], hovered ? clr->main.grad_2.Value : clr->main.accent.Value, 12.f, dynamic_easing);

    draw->text_clipped(window->DrawList, font->get(inter_medium_data, 13), total.Min - SCALE(0, 3), total.Max, draw->get_clr(state->text), "Don't have an account yet?");

    const int text_vtx_start = window->DrawList->VtxBuffer.Size;
    draw->text_clipped(window->DrawList, font->get(inter_medium_data, 13), button_rect.Min - SCALE(0, 3), button_rect.Max, draw->get_clr(clr->main.text), "REGISTER");
    const int text_vtx_end = window->DrawList->VtxBuffer.Size;
    draw->set_linear_color_alpha(window->DrawList, text_vtx_start, text_vtx_end, ImVec2(button_rect.Min.x, button_rect.Min.y), ImVec2(button_rect.GetCenter().x, button_rect.Max.y), draw->get_clr(state->rect[0]), draw->get_clr(state->rect[1]));
    draw->set_linear_color_alpha(window->DrawList, text_vtx_start, text_vtx_end, ImVec2(button_rect.GetCenter().x, button_rect.Min.y), ImVec2(button_rect.Max.x, button_rect.Max.y), draw->get_clr(state->rect[1]), draw->get_clr(state->rect[2]));

    for (int i = 0; i < elements->button.reg_circ_count; i++)
    {
        if (state->clicked)
            gui->easing(state->alpha[i], (i > 0 ? state->alpha[i - 1] < 0.01f : true) ? 0.f : 1.f, 8.f, static_easing);
        else
            gui->easing(state->alpha[i], (i > 0 ? state->alpha[i - 1] > 0.99f : true) ? 1.f : 0.f, 8.f, static_easing);

        float x_pos = button_rect.Min.x + SCALE(elements->button.reg_radius) + i * (SCALE(elements->button.reg_radius * 2) + circ_padding);
        draw->circle_filled(window->DrawList, ImVec2(x_pos, button_rect.Max.y - SCALE(elements->button.reg_radius)), SCALE(elements->button.reg_radius), draw->get_clr(ImLerp(state->rect[i > 7 ? 1 : 0], state->rect[i > 7 ? 2 : 1], (i > 7 ? (i - 7) : i + 1) * 0.13f), state->alpha[i]), 60);
    }

    return pressed;
}