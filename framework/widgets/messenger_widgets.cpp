#include "../headers/includes.h"
#include "../headers/messenger_widgets.h"
#include <cstring>
#include "../headers/widgets.h"

// ============================================================================
// ЭКРАН ВХОДА
// ============================================================================
void c_messenger_widgets::render_login_screen()
{
    struct anim_state
    {
        ImVec4 button_bg{ clr->button.primary.Value };
        float  button_alpha{ 0.0f };
        float  glow_animation{ 0.0f };  // Анимация подсветки

        // Для перетаскивания карточки
        ImVec2 card_offset{ 0.0f, 0.0f };
        bool   dragging{ false };
        ImVec2 drag_start_mouse{ 0.0f, 0.0f };
        ImVec2 drag_start_offset{ 0.0f, 0.0f };
    };

    ImGuiWindow* window = gui->get_window();
    if (!window || window->SkipItems) return;

    const ImGuiID id = window->GetID("login_screen");
    anim_state* state = gui->anim_container<anim_state>(id);

    ImVec2 screen_size = SCALE(var->window.size);

    // Компактная карточка фиксированной ширины, но ниже по высоте
    ImVec2 card_size = SCALE(420, 520);
    ImVec2 card_pos{
        (screen_size.x - card_size.x) * 0.5f,
        (screen_size.y - card_size.y) * 0.5f
    };

    // Область "хедера", за которую можно тянуть (только верх, без полей ввода)
    ImVec2 drag_area_min = card_pos;
    ImVec2 drag_area_max = ImVec2(card_pos.x + card_size.x, card_pos.y + SCALE(110));
    ImRect drag_rect(drag_area_min, drag_area_max);

    ImVec2 mouse = gui->mouse_pos();
    bool drag_hovered = drag_rect.Contains(mouse);

    if (!state->dragging)
    {
        if (drag_hovered && gui->mouse_clicked(mouse_button_left))
        {
            state->dragging = true;
            state->drag_start_mouse = mouse;
            state->drag_start_offset = state->card_offset;
        }
    }
    else
    {
        // Пока кнопка мыши зажата — двигаем карточку
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            ImVec2 delta = mouse - state->drag_start_mouse;
            ImVec2 new_offset = state->drag_start_offset + delta;

            // Ограничиваем движение, чтобы карточка не выходила за пределы окна
            float min_x = -card_pos.x;
            float max_x = screen_size.x - (card_pos.x + card_size.x);
            float min_y = -card_pos.y;
            float max_y = screen_size.y - (card_pos.y + card_size.y);

            new_offset.x = ImClamp(new_offset.x, min_x, max_x);
            new_offset.y = ImClamp(new_offset.y, min_y, max_y);

            state->card_offset = new_offset;
        }
        else
        {
            state->dragging = false;
        }
    }

    // Применяем смещение
    card_pos.x += state->card_offset.x;
    card_pos.y += state->card_offset.y;

    float card_rounding = SCALE(24);

    // Тень
    draw->shadow_rect(window->DrawList, card_pos, card_pos + card_size,
        draw->get_clr(ImVec4(0, 0, 0, 0.35f)), SCALE(30), ImVec2(0, SCALE(8)),
        draw_flags_round_corners_all, card_rounding);

    // Карточка
    draw->rect_filled(window->DrawList, card_pos, card_pos + card_size,
        draw->get_clr(clr->cards.background), card_rounding);

    draw->rect(window->DrawList, card_pos, card_pos + card_size,
        draw->get_clr(clr->cards.border, 0.4f), card_rounding,
        draw_flags_round_corners_all, SCALE(1));

    // Аватар сверху
    float avatar_radius = SCALE(32);
    ImVec2 avatar_center = ImVec2(
        card_pos.x + card_size.x * 0.5f,
        card_pos.y + SCALE(72)
    );

    draw->circle_filled(window->DrawList, avatar_center, avatar_radius,
        draw->get_clr(clr->main.hover), 48);

    draw->circle(window->DrawList,
        avatar_center, avatar_radius,
        draw->get_clr(clr->main.outline, 0.35f), 48, SCALE(2));

    // Иконка внутри (можно заменить на свой глиф)
    draw->text_clipped(window->DrawList, font->get(icons_data, 26),
        avatar_center - ImVec2(avatar_radius, avatar_radius),
        avatar_center + ImVec2(avatar_radius, avatar_radius),
        draw->get_clr(clr->button.text), "U", nullptr, nullptr, ImVec2(0.5f, 0.5f));

    // Заголовок
    ImVec2 title_min = ImVec2(card_pos.x, avatar_center.y + SCALE(28));
    ImVec2 title_max = ImVec2(card_pos.x + card_size.x, title_min.y + SCALE(40));
    draw->text_clipped(window->DrawList, font->get(inter_bold_data, 24),
        title_min, title_max,
        draw->get_clr(clr->main.text), "Welcome Back", nullptr, nullptr, ImVec2(0.5f, 0.5f));

    // Подзаголовок
    ImVec2 sub_min = ImVec2(card_pos.x, title_max.y);
    ImVec2 sub_max = ImVec2(card_pos.x + card_size.x, sub_min.y + SCALE(24));
    draw->text_clipped(window->DrawList, font->get(inter_medium_data, 13),
        sub_min, sub_max,
        draw->get_clr(clr->main.text_inactive),
        "Please enter your info to sign in", nullptr, nullptr, ImVec2(0.5f, 0.5f));

    // --- Ряд соц‑кнопок ---
    float social_y = sub_max.y + SCALE(28);
    float social_h = SCALE(40);
    float gap      = SCALE(10);
    float btn_w    = (card_size.x - SCALE(40) - gap * 2.0f) / 3.0f;
    ImVec2 social_start = ImVec2(card_pos.x + SCALE(20), social_y);

    const char* labels[3] = { "Y", "H", "O" }; // Y=Google, H=Facebook, O=Apple

    for (int i = 0; i < 3; ++i)
    {
        ImVec2 b_min = ImVec2(social_start.x + (btn_w + gap) * i, social_y);
        ImVec2 b_max = ImVec2(b_min.x + btn_w, social_y + social_h);
        ImRect b_rect(b_min, b_max);

        bool hovered = b_rect.Contains(gui->mouse_pos());
        ImU32 bg = draw->get_clr(hovered ? clr->button.secondary : clr->window.background);

        draw->rect_filled(window->DrawList, b_rect.Min, b_rect.Max,
            bg, SCALE(10));

        draw->rect(window->DrawList, b_rect.Min, b_rect.Max,
            draw->get_clr(clr->cards.border, 0.35f), SCALE(10),
            draw_flags_round_corners_all, SCALE(1));

        draw->text_clipped(window->DrawList, font->get(icons_data, 16),
            b_rect.Min, b_rect.Max,
            draw->get_clr(clr->main.text), labels[i], nullptr, nullptr, ImVec2(0.5f, 0.5f));
    }

    // --- Поля ввода ---
    float fields_left = card_pos.x + SCALE(24);
    float fields_right = card_pos.x + card_size.x - SCALE(24);
    // Email / Phone label
    ImVec2 email_label_min(fields_left, social_y + social_h + SCALE(24));
    ImVec2 email_label_max(fields_right, email_label_min.y + SCALE(14));
    draw->text_clipped(window->DrawList, font->get(inter_medium_data, 12),
        email_label_min, email_label_max,
        draw->get_clr(clr->main.text_secondary), "Phone Number", nullptr, nullptr, ImVec2(0.0f, 0.5f));

    // Поле телефона
    gui->set_pos(ImVec2(fields_left, email_label_max.y + SCALE(4)), pos_all);
    widgets->text_field("Phone", "A", app_state->login_phone, sizeof(app_state->login_phone),nullptr, ImVec2{ 370, 50 });

    // Password label — чуть ближе к полю телефона
    ImVec2 pass_label_min(fields_left, email_label_max.y + SCALE(52));
    ImVec2 pass_label_max(fields_right, pass_label_min.y + SCALE(18));
    draw->text_clipped(window->DrawList, font->get(inter_medium_data, 12),
        pass_label_min, pass_label_max,
        draw->get_clr(clr->main.text_secondary), "Password", nullptr, nullptr, ImVec2(0.0f, 0.5f));

    // Поле пароля
    gui->set_pos(ImVec2(fields_left, pass_label_max.y + SCALE(4)), pos_all);
    widgets->text_field("Password", "B", app_state->login_password, sizeof(app_state->login_password), nullptr, ImVec2{ 370, 50 });
    // --- Кнопка Sign in --- чуть ниже, чтобы не наезжала на элементы
    ImVec2 sign_min(fields_left, card_pos.y + card_size.y - SCALE(100));
    ImVec2 sign_max(fields_right, sign_min.y + SCALE(44));
    ImRect sign_rect(sign_min, sign_max);

    bool sign_hovered = sign_rect.Contains(gui->mouse_pos());
    bool sign_clicked = sign_hovered && gui->mouse_clicked(mouse_button_left);

    if (sign_clicked)
    {
        if (chat_manager->Login(app_state->login_phone, app_state->login_password))
        {
            // запуск анимации расширения (ниже добавим поля в AppState)
            app_state->auth_to_main_progress = 0.0f;
            app_state->auth_expanding = true;
        }
    }

    // Анимация подсветки при наведении
    float target_glow = sign_hovered ? 1.0f : 0.0f;
    gui->easing(state->glow_animation, target_glow, 15.f, dynamic_easing);

    // Градиентные цвета для кнопки
    ImVec4 grad_color_1 = clr->main.accent.Value;
    ImVec4 grad_color_2 = clr->main.grad_1.Value;
    
    // Усиливаем яркость при наведении
    if (sign_hovered)
    {
        grad_color_1 = ImLerp(grad_color_1, ImVec4(grad_color_1.x * 1.2f, grad_color_1.y * 1.2f, grad_color_1.z * 1.2f, grad_color_1.w), state->glow_animation);
        grad_color_2 = ImLerp(grad_color_2, ImVec4(grad_color_2.x * 1.2f, grad_color_2.y * 1.2f, grad_color_2.z * 1.2f, grad_color_2.w), state->glow_animation);
    }

    ImU32 grad_col_1 = draw->get_clr(grad_color_1);
    ImU32 grad_col_2 = draw->get_clr(grad_color_2);

    if (sign_hovered)
    {
        draw->shadow_rect(window->DrawList, sign_rect.Min, sign_rect.Max,
            draw->get_clr(ImVec4(0, 0, 0, 0.18f)), SCALE(10), ImVec2(0, SCALE(3)),
            draw_flags_round_corners_all, SCALE(12));
    }

    // Градиентная кнопка (слева направо)
    draw->rect_filled_multi_color(window->DrawList, sign_rect.Min, sign_rect.Max,
        grad_col_1, grad_col_2, grad_col_2, grad_col_1, SCALE(12));

    // Анимированная подсветка с градиентом
    if (state->glow_animation > 0.01f)
    {
        float time = ImGui::GetTime();
        float glow_offset = sinf(time * 2.0f) * 0.3f + 0.7f;  // Плавное движение подсветки
        
        ImVec4 glow_color_1 = ImVec4(1.0f, 1.0f, 1.0f, 0.15f * state->glow_animation * glow_offset);
        ImVec4 glow_color_2 = ImVec4(1.0f, 1.0f, 1.0f, 0.05f * state->glow_animation * glow_offset);
        
        ImU32 glow_col_1 = draw->get_clr(glow_color_1);
        ImU32 glow_col_2 = draw->get_clr(glow_color_2);
        
        // Подсветка поверх кнопки
        draw->rect_filled_multi_color(window->DrawList, sign_rect.Min, sign_rect.Max,
            glow_col_1, glow_col_2, glow_col_2, glow_col_1, SCALE(12));
    }

    draw->text_clipped(window->DrawList, font->get(inter_bold_data, 15),
        sign_rect.Min, sign_rect.Max,
        draw->get_clr(clr->button.text), "Sign in", nullptr, nullptr, ImVec2(0.5f, 0.5f));

    gui->item_size(sign_rect);

    // Низ: "Don't have an account yet? Sign up"
    ImVec2 bottom_min(card_pos.x, card_pos.y + card_size.y - SCALE(40));
    ImVec2 bottom_max(card_pos.x + card_size.x, bottom_min.y + SCALE(34));

    const char* left_txt = "Don't have an account yet? ";
    const char* right_txt = "Sign up";

    // Левая часть (серый текст)
    draw->text_clipped(window->DrawList, font->get(inter_medium_data, 12),
        bottom_min-SCALE(-5,15), bottom_max-SCALE(-5,15),
        draw->get_clr(clr->main.text_inactive), left_txt, nullptr, nullptr, ImVec2(0.5f, 0.5f));

    // Правая часть — кликабельная
    ImVec2 sign_up_min = bottom_min;
    ImVec2 sign_up_max = bottom_max;
    ImRect sign_up_rect(sign_up_min, sign_up_max);
    bool sign_up_hovered = sign_up_rect.Contains(gui->mouse_pos());
    bool sign_up_clicked = sign_up_hovered && gui->mouse_clicked(mouse_button_left);

    if (sign_up_clicked)
        app_state->current_screen = AppScreen::Registration;

    draw->text_clipped(window->DrawList, font->get(inter_medium_data, 12),
        bottom_min, bottom_max,
        draw->get_clr(sign_up_hovered ? clr->main.accent : clr->main.text),
        right_txt, nullptr, nullptr, ImVec2(0.5f, 0.5f));
}

// ============================================================================
// ЭКРАН РЕГИСТРАЦИИ
// ============================================================================
void c_messenger_widgets::render_registration_screen()
{
    struct anim_state
    {
        ImVec4 button_bg{ clr->button.primary.Value };
        float  button_alpha{ 0.0f };
        float  glow_animation{ 0.0f };  // Анимация подсветки

        // Для перетаскивания карточки регистрации
        ImVec2 card_offset{ 0.0f, 0.0f };
        bool   dragging{ false };
        ImVec2 drag_start_mouse{ 0.0f, 0.0f };
        ImVec2 drag_start_offset{ 0.0f, 0.0f };
    };

    ImGuiWindow* window = gui->get_window();
    if (!window || window->SkipItems) return;

    const ImGuiID id = window->GetID("registration_screen");
    anim_state* state = gui->anim_container<anim_state>(id);

    ImVec2 screen_size = SCALE(var->window.size);
    // Компактная карточка фиксированной ширины, немного ниже по высоте
    ImVec2 card_size = SCALE(420, 430);
    ImVec2 card_pos{
        (screen_size.x - card_size.x) * 0.5f,
        (screen_size.y - card_size.y) * 0.5f
    };

    // Область хедера для перетаскивания (только верхняя часть)
    ImVec2 drag_area_min = card_pos;
    ImVec2 drag_area_max = ImVec2(card_pos.x + card_size.x, card_pos.y + SCALE(110));
    ImRect drag_rect(drag_area_min, drag_area_max);

    ImVec2 mouse = gui->mouse_pos();
    bool drag_hovered = drag_rect.Contains(mouse);

    if (!state->dragging)
    {
        if (drag_hovered && gui->mouse_clicked(mouse_button_left))
        {
            state->dragging = true;
            state->drag_start_mouse = mouse;
            state->drag_start_offset = state->card_offset;
        }
    }
    else
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            ImVec2 delta = mouse - state->drag_start_mouse;
            ImVec2 new_offset = state->drag_start_offset + delta;

            float min_x = -card_pos.x;
            float max_x = screen_size.x - (card_pos.x + card_size.x);
            float min_y = -card_pos.y;
            float max_y = screen_size.y - (card_pos.y + card_size.y);

            new_offset.x = ImClamp(new_offset.x, min_x, max_x);
            new_offset.y = ImClamp(new_offset.y, min_y, max_y);

            state->card_offset = new_offset;
        }
        else
        {
            state->dragging = false;
        }
    }

    card_pos.x += state->card_offset.x;
    card_pos.y += state->card_offset.y;

    float card_rounding = SCALE(24);

    // Тень карточки
    draw->shadow_rect(window->DrawList, card_pos, card_pos + card_size,
        draw->get_clr(ImVec4(0, 0, 0, 0.35f)), SCALE(30), ImVec2(0, SCALE(8)),
        draw_flags_round_corners_all, card_rounding);

    // Карточка
    draw->rect_filled(window->DrawList, card_pos, card_pos + card_size,
        draw->get_clr(clr->cards.background), card_rounding);

    draw->rect(window->DrawList, card_pos, card_pos + card_size,
        draw->get_clr(clr->cards.border, 0.4f), card_rounding,
        draw_flags_round_corners_all, SCALE(1));

    // Аватар (как на логине, для консистентности)
    float avatar_radius = SCALE(28);
    ImVec2 avatar_center = ImVec2(
        card_pos.x + card_size.x * 0.5f,
        card_pos.y + SCALE(64)
    );

    draw->circle_filled(window->DrawList, avatar_center, avatar_radius,
        draw->get_clr(clr->main.hover), 48);

    draw->circle(window->DrawList,
        avatar_center, avatar_radius,
        draw->get_clr(clr->main.outline, 0.35f), 48, SCALE(2));

    draw->text_clipped(window->DrawList, font->get(icons_data, 22),
        avatar_center - ImVec2(avatar_radius, avatar_radius),
        avatar_center + ImVec2(avatar_radius, avatar_radius),
        draw->get_clr(clr->button.text), "P", nullptr, nullptr, ImVec2(0.5f, 0.5f)); // P=Plus

    // Заголовок
    ImVec2 title_min = ImVec2(card_pos.x, avatar_center.y + SCALE(24));
    ImVec2 title_max = ImVec2(card_pos.x + card_size.x, title_min.y + SCALE(36));
    draw->text_clipped(window->DrawList, font->get(inter_bold_data, 24),
        title_min, title_max,
        draw->get_clr(clr->main.text), "Create Account", nullptr, nullptr, ImVec2(0.5f, 0.5f));

    // Подзаголовок
    ImVec2 sub_min = ImVec2(card_pos.x, title_max.y);
    ImVec2 sub_max = ImVec2(card_pos.x + card_size.x, sub_min.y + SCALE(24));
    draw->text_clipped(window->DrawList, font->get(inter_medium_data, 13),
        sub_min, sub_max,
        draw->get_clr(clr->main.text_inactive),
        "Enter your phone number to sign up", nullptr, nullptr, ImVec2(0.5f, 0.5f));

    // Поле ввода телефона
    float fields_left = card_pos.x + SCALE(24);
    float fields_right = card_pos.x + card_size.x - SCALE(24);

    ImVec2 phone_label_min(fields_left, sub_max.y + SCALE(24));
    ImVec2 phone_label_max(fields_right, phone_label_min.y + SCALE(18));
    draw->text_clipped(window->DrawList, font->get(inter_medium_data, 12),
        phone_label_min, phone_label_max,
        draw->get_clr(clr->main.text_secondary), "Phone Number", nullptr, nullptr, ImVec2(0.0f, 0.5f));

    // Само поле — с небольшим отступом от лейбла
    gui->set_pos(ImVec2(fields_left, phone_label_max.y + SCALE(8)), pos_all);
    widgets->text_field("Phone Number", "A", app_state->phone_input, sizeof(app_state->phone_input), nullptr, ImVec2{ 370, 50 });

    // Кнопка отправки кода — визуально под полем, а не "привязана" к низу карточки
    ImVec2 send_min(fields_left, phone_label_max.y + SCALE(80));
    ImVec2 send_max(fields_right, send_min.y + SCALE(44));
    ImRect send_rect(send_min, send_max);
    bool send_hovered = send_rect.Contains(gui->mouse_pos());
    bool send_clicked = send_hovered && gui->mouse_clicked(mouse_button_left);

    if (send_clicked)
    {
        if (chat_manager->SendVerificationCode(app_state->phone_input))
        {
            app_state->current_screen = AppScreen::PhoneVerification;
        }
    }

    // Анимация подсветки при наведении
    float target_glow = send_hovered ? 1.0f : 0.0f;
    gui->easing(state->glow_animation, target_glow, 15.f, dynamic_easing);

    // Градиентные цвета для кнопки
    ImVec4 grad_color_1 = clr->main.accent.Value;
    ImVec4 grad_color_2 = clr->main.grad_1.Value;
    
    // Усиливаем яркость при наведении
    if (send_hovered)
    {
        grad_color_1 = ImLerp(grad_color_1, ImVec4(grad_color_1.x * 1.2f, grad_color_1.y * 1.2f, grad_color_1.z * 1.2f, grad_color_1.w), state->glow_animation);
        grad_color_2 = ImLerp(grad_color_2, ImVec4(grad_color_2.x * 1.2f, grad_color_2.y * 1.2f, grad_color_2.z * 1.2f, grad_color_2.w), state->glow_animation);
    }

    ImU32 grad_col_1 = draw->get_clr(grad_color_1);
    ImU32 grad_col_2 = draw->get_clr(grad_color_2);

    if (send_hovered)
    {
        draw->shadow_rect(window->DrawList, send_rect.Min, send_rect.Max,
            draw->get_clr(ImVec4(0, 0, 0, 0.18f)), SCALE(10), ImVec2(0, SCALE(3)),
            draw_flags_round_corners_all, SCALE(12));
    }

    // Градиентная кнопка (слева направо)
    draw->rect_filled_multi_color(window->DrawList, send_rect.Min, send_rect.Max,
        grad_col_1, grad_col_2, grad_col_2, grad_col_1, SCALE(12));

    // Анимированная подсветка с градиентом
    if (state->glow_animation > 0.01f)
    {
        float time = ImGui::GetTime();
        float glow_offset = sinf(time * 2.0f) * 0.3f + 0.7f;  // Плавное движение подсветки
        
        ImVec4 glow_color_1 = ImVec4(1.0f, 1.0f, 1.0f, 0.15f * state->glow_animation * glow_offset);
        ImVec4 glow_color_2 = ImVec4(1.0f, 1.0f, 1.0f, 0.05f * state->glow_animation * glow_offset);
        
        ImU32 glow_col_1 = draw->get_clr(glow_color_1);
        ImU32 glow_col_2 = draw->get_clr(glow_color_2);
        
        // Подсветка поверх кнопки
        draw->rect_filled_multi_color(window->DrawList, send_rect.Min, send_rect.Max,
            glow_col_1, glow_col_2, glow_col_2, glow_col_1, SCALE(12));
    }

    draw->text_clipped(window->DrawList, font->get(inter_bold_data, 15),
        send_rect.Min, send_rect.Max,
        draw->get_clr(clr->button.text), "Send code", nullptr, nullptr, ImVec2(0.5f, 0.5f));

    gui->item_size(send_rect);

    // Низ: "Already have an account? Sign in"
    ImVec2 bottom_min(card_pos.x, card_pos.y + card_size.y - SCALE(40));
    ImVec2 bottom_max(card_pos.x + card_size.x, bottom_min.y + SCALE(34));

    const char* left_txt = "Already have an account? ";
    const char* right_txt = "Sign in";

    draw->text_clipped(window->DrawList, font->get(inter_medium_data, 12),
        bottom_min-SCALE(-5,15), bottom_max-SCALE(-5,15),
        draw->get_clr(clr->main.text_inactive), left_txt, nullptr, nullptr, ImVec2(0.5f, 0.5f));

    ImRect sign_in_rect(bottom_min, bottom_max);
    bool sign_in_hovered = sign_in_rect.Contains(gui->mouse_pos());
    bool sign_in_clicked = sign_in_hovered && gui->mouse_clicked(mouse_button_left);

    if (sign_in_clicked)
        app_state->current_screen = AppScreen::Login;

    draw->text_clipped(window->DrawList, font->get(inter_medium_data, 12),
        bottom_min, bottom_max,
        draw->get_clr(sign_in_hovered ? clr->main.accent : clr->main.text),
        right_txt, nullptr, nullptr, ImVec2(0.5f, 0.5f));
}

// ============================================================================
// ЭКРАН ПОДТВЕРЖДЕНИЯ ТЕЛЕФОНА
// ============================================================================
void c_messenger_widgets::render_phone_verification_screen()
{
    struct anim_state
    {
        ImVec4 button_bg{ clr->main.accent };
        float button_alpha{ 0 };
        float shake_offset{ 0.0f };  // Смещение для тряски влево-вправо
        float error_animation_timer{ 0.0f };  // Таймер анимации ошибки
        bool is_error_state{ false };  // Флаг состояния ошибки
        float glow_animation{ 0.0f };  // Анимация подсветки
    };

    ImGuiWindow* window = gui->get_window();
    if (window->SkipItems) return;

    const ImGuiID id = window->GetID("verification_screen");
    anim_state* state = gui->anim_container<anim_state>(id);

    ImVec2 screen_size = SCALE(var->window.size);
    ImVec2 form_size = SCALE(400, 450);
    ImVec2 form_pos = ImVec2((screen_size.x - form_size.x) / 2, (screen_size.y - form_size.y) / 2);


    // Modern verification form
    float form_rounding = SCALE(16);
    
    // Subtle shadow
    draw->shadow_rect(window->DrawList, form_pos, form_pos + form_size,
        draw->get_clr(ImVec4(0, 0, 0, 0.2f)), SCALE(24), ImVec2(0, SCALE(4)),
        draw_flags_round_corners_all, form_rounding);
    
    // Form background
    draw->rect_filled(window->DrawList, form_pos, form_pos + form_size,
        draw->get_clr(clr->cards.background), form_rounding);
    
    // Subtle border
    draw->rect(window->DrawList, form_pos, form_pos + form_size,
        draw->get_clr(clr->cards.border, 0.5f), form_rounding, draw_flags_round_corners_all, SCALE(1));

    // Заголовок
    draw->text_clipped(window->DrawList, font->get(inter_bold_data, 24),
        form_pos + SCALE(0, 40), form_pos + SCALE(form_size.x, 80),
        draw->get_clr(clr->main.text), "Verification", nullptr, nullptr, ImVec2(0.5f, 0.5f));

    // Информация
    std::string info_text = "Code sent to " + app_state->sent_to_phone;
    draw->text_clipped(window->DrawList, font->get(inter_medium_data, 13),
        form_pos + SCALE(0, 80), form_pos + SCALE(form_size.x, 110),
        draw->get_clr(clr->main.text_inactive), info_text.c_str(), nullptr, nullptr, ImVec2(0.5f, 0.5f));

    // Поле ввода кода
    gui->set_pos(form_pos + SCALE(40, 150), pos_all);
    widgets->text_field("Verification Code", "C", app_state->verification_code, sizeof(app_state->verification_code));

    // Кнопка подтверждения
    gui->set_pos(form_pos + SCALE(40, 250), pos_all);

    ImRect button_rect(form_pos + SCALE(40, 250), form_pos + SCALE(360, 300));
    bool hovered = button_rect.Contains(gui->mouse_pos());
    bool clicked = hovered && gui->mouse_clicked(mouse_button_left);

    // Проверка кода при клике
    if (clicked)
    {
        std::string code = app_state->verification_code;
        bool is_valid = (code == "1234" || code.length() >= 4);
        
        if (!is_valid)
        {
            // Запускаем анимацию ошибки
            state->is_error_state = true;
            state->error_animation_timer = 20.0f;  // Длительность анимации (1 секунда)
        }
        else
        {
            // Код правильный - вызываем верификацию
        chat_manager->VerifyCode(app_state->verification_code);
            state->is_error_state = false;
            state->error_animation_timer = 0.0f;
        }
    }

    // Обработка анимации ошибки
    if (state->is_error_state && state->error_animation_timer > 0.0f)
    {
        // Уменьшаем таймер
        state->error_animation_timer -= gui->fixed_speed(60.0f);  // 60 кадров на секунду
        
        if (state->error_animation_timer <= 0.0f)
        {
            state->error_animation_timer = 0.0f;
            state->is_error_state = false;
        }
        
        // Тряска: синусоидальное движение влево-вправо
        float shake_intensity = SCALE(4.0f);  // Амплитуда тряски
        float shake_speed = 20.0f;  // Скорость тряски
        float time = ImGui::GetTime();
        state->shake_offset = sinf(time * shake_speed) * shake_intensity * state->error_animation_timer;
    }
    else
    {
        // Плавно возвращаем смещение к нулю
        gui->easing(state->shake_offset, 0.0f, 15.f, dynamic_easing);
    }

    // Анимация подсветки при наведении (только если не ошибка)
    float target_glow = (hovered && !state->is_error_state) ? 1.0f : 0.0f;
    gui->easing(state->glow_animation, target_glow, 15.f, dynamic_easing);
    
    float button_rounding = SCALE(8);
    
    // Применяем смещение для тряски
    ImRect final_button_rect = button_rect;
    final_button_rect.Min.x += state->shake_offset;
    final_button_rect.Max.x += state->shake_offset;
    
    // Определяем цвета кнопки
    ImVec4 grad_color_1, grad_color_2;
    if (state->is_error_state && state->error_animation_timer > 0.0f)
    {
        // Красный градиент при ошибке
        grad_color_1 = ImVec4(0.9f, 0.2f, 0.2f, 1.0f);
        grad_color_2 = ImVec4(0.7f, 0.15f, 0.15f, 1.0f);
    }
    else
    {
        // Обычный градиент
        grad_color_1 = clr->main.accent.Value;
        grad_color_2 = clr->main.grad_1.Value;
        
        // Усиливаем яркость при наведении
    if (hovered)
    {
            grad_color_1 = ImLerp(grad_color_1, ImVec4(grad_color_1.x * 1.2f, grad_color_1.y * 1.2f, grad_color_1.z * 1.2f, grad_color_1.w), state->glow_animation);
            grad_color_2 = ImLerp(grad_color_2, ImVec4(grad_color_2.x * 1.2f, grad_color_2.y * 1.2f, grad_color_2.z * 1.2f, grad_color_2.w), state->glow_animation);
        }
    }

    ImU32 grad_col_1 = draw->get_clr(grad_color_1);
    ImU32 grad_col_2 = draw->get_clr(grad_color_2);
    
    // Subtle shadow on hover (только если не ошибка)
    if (hovered && !state->is_error_state)
    {
        draw->shadow_rect(window->DrawList, final_button_rect.Min, final_button_rect.Max,
            draw->get_clr(ImVec4(0, 0, 0, 0.15f)), SCALE(8), ImVec2(0, SCALE(2)),
            draw_flags_round_corners_all, button_rounding);
    }
    
    // Градиентная кнопка (слева направо)
    draw->rect_filled_multi_color(window->DrawList, final_button_rect.Min, final_button_rect.Max,
        grad_col_1, grad_col_2, grad_col_2, grad_col_1, button_rounding);

    // Анимированная подсветка с градиентом (только если не ошибка)
    if (state->glow_animation > 0.01f && !state->is_error_state)
    {
        float time = ImGui::GetTime();
        float glow_offset = sinf(time * 2.0f) * 0.3f + 0.7f;  // Плавное движение подсветки
        
        ImVec4 glow_color_1 = ImVec4(1.0f, 1.0f, 1.0f, 0.15f * state->glow_animation * glow_offset);
        ImVec4 glow_color_2 = ImVec4(1.0f, 1.0f, 1.0f, 0.05f * state->glow_animation * glow_offset);
        
        ImU32 glow_col_1 = draw->get_clr(glow_color_1);
        ImU32 glow_col_2 = draw->get_clr(glow_color_2);
        
        // Подсветка поверх кнопки
        draw->rect_filled_multi_color(window->DrawList, final_button_rect.Min, final_button_rect.Max,
            glow_col_1, glow_col_2, glow_col_2, glow_col_1, button_rounding);
    }

    // Button text - always white
    ImU32 verify_text_color = draw->get_clr(clr->button.text);
    
    draw->text_clipped(window->DrawList, font->get(inter_bold_data, 14),
        final_button_rect.Min, final_button_rect.Max,
        verify_text_color, "VERIFY", nullptr, nullptr, ImVec2(0.5f, 0.5f));

    gui->item_size(final_button_rect);

    // Информация о коде
    draw->text_clipped(window->DrawList, font->get(inter_medium_data, 11),
        form_pos + SCALE(0, 320), form_pos + SCALE(form_size.x, 350),
        draw->get_clr(clr->main.text_muted), "Hint: Use code '1234'", nullptr, nullptr, ImVec2(0.5f, 0.5f));
}

// ============================================================================
// ГЛАВНЫЙ ИНТЕРФЕЙС МЕССЕНДЖЕРА
// ============================================================================
void c_messenger_widgets::render_main_messenger()
{
    ImGuiWindow* window = gui->get_window();
    if (window->SkipItems) return;

    ImVec2 screen_size = SCALE(var->window.size);

    // Плавная анимация перехода от компактного состояния после авторизации
    if (app_state->auth_expanding)
    {
        gui->easing(app_state->auth_to_main_progress, 1.0f, 8.0f, dynamic_easing);
        if (app_state->auth_to_main_progress > 0.99f)
        {
            app_state->auth_to_main_progress = 1.0f;
            app_state->auth_expanding = false;
        }
    }

    // Рендерим три панели
    render_left_panel();
    render_center_panel();
    
    // Чат инфо диалог рендерится поверх всех панелей
    if (app_state->show_right_panel || app_state->chat_info_anim_alpha > 0.001f)
    {
        render_chat_info_dialog();
    }


    // Диалог создания чата рендерится поверх всех панелей
    // ВАЖНО: Вызывается ПОСЛЕ всех панелей, чтобы быть сверху по Z-order
    if (app_state->show_create_chat_dialog || app_state->create_dialog_anim_alpha > 0.001f)
    {
        render_create_chat_dialog();
    }
}

// ============================================================================
// ЛЕВАЯ ПАНЕЛЬ - СПИСОК ЧАТОВ
// ============================================================================
void c_messenger_widgets::render_left_panel()
{
    struct anim_state
    {
        float create_glow_animation{ 0.0f };  // Анимация подсветки для кнопки создания чата
        float conf_glow_animation{ 0.0f };   // Анимация подсветки для кнопки конференций
    };

    ImGuiWindow* window = gui->get_window();
    if (window->SkipItems) return;

    const ImGuiID id = window->GetID("left_panel");
    anim_state* state = gui->anim_container<anim_state>(id);

    ImVec2 screen_size = SCALE(var->window.size);
    
    // Делаем ширину левой панели зависящей от прогресса анимации auth_to_main_progress
    float t = ImClamp(app_state->auth_to_main_progress, 0.0f, 1.0f);
    float width_factor = 0.6f + 0.4f * t; // от 60% до 100%
    float panel_width = SCALE(app_state->left_panel_width * width_factor);
    
    ImVec2 panel_pos = SCALE(0, 0);
    ImVec2 panel_size = ImVec2(panel_width, screen_size.y);

    // Modern panel background
    draw->rect_filled(window->DrawList, panel_pos, panel_pos + panel_size,
        draw->get_clr(clr->messages.background), 0);

    // Modern divider - subtle and refined
    draw->rect_filled(window->DrawList, 
        ImVec2(panel_pos.x + panel_width - SCALE(1), panel_pos.y),
        ImVec2(panel_pos.x + panel_width, panel_pos.y + panel_size.y),
        draw->get_clr(clr->main.divider), 0);

    // Заголовок панели
    ImVec2 header_pos = panel_pos + SCALE(20, 20);
    draw->text_clipped(window->DrawList, font->get(inter_bold_data, 18),
        header_pos, header_pos + SCALE(panel_width - 120, 30), // Reduced width to fit buttons
        draw->get_clr(clr->main.text), "Chats", nullptr, nullptr, ImVec2(0.0f, 0.5f));

    // Кнопка создания чата
    ImVec2 create_btn_pos = ImVec2(panel_pos.x + panel_width - SCALE(50), panel_pos.y + SCALE(15));
    ImRect create_btn_rect(create_btn_pos, create_btn_pos + SCALE(35, 35));
    bool create_hovered = create_btn_rect.Contains(gui->mouse_pos());
    bool create_clicked = create_hovered && gui->mouse_clicked(mouse_button_left);

    // Кнопка конференций (слева от создания чата)
    ImVec2 conf_btn_pos = ImVec2(panel_pos.x + panel_width - SCALE(95), panel_pos.y + SCALE(15));
    ImRect conf_btn_rect(conf_btn_pos, conf_btn_pos + SCALE(35, 35));
    bool conf_hovered = conf_btn_rect.Contains(gui->mouse_pos());
    bool conf_clicked = conf_hovered && gui->mouse_clicked(mouse_button_left);

    if (conf_clicked) {
        app_state->current_screen = AppScreen::Conference;
    }
    
    // Анимация подсветки для кнопки конференций
    float conf_target_glow = conf_hovered ? 1.0f : 0.0f;
    gui->easing(state->conf_glow_animation, conf_target_glow, 15.f, dynamic_easing);

    // Градиентные цвета для кнопки конференций
    ImVec4 conf_grad_color_1 = clr->conference.primary.Value;
    ImVec4 conf_grad_color_2 = clr->main.grad_1.Value;
    
    if (conf_hovered)
    {
        conf_grad_color_1 = ImLerp(conf_grad_color_1, ImVec4(conf_grad_color_1.x * 1.2f, conf_grad_color_1.y * 1.2f, conf_grad_color_1.z * 1.2f, conf_grad_color_1.w), state->conf_glow_animation);
        conf_grad_color_2 = ImLerp(conf_grad_color_2, ImVec4(conf_grad_color_2.x * 1.2f, conf_grad_color_2.y * 1.2f, conf_grad_color_2.z * 1.2f, conf_grad_color_2.w), state->conf_glow_animation);
    }

    ImU32 conf_col_1 = draw->get_clr(conf_grad_color_1);
    ImU32 conf_col_2 = draw->get_clr(conf_grad_color_2);
    
    // Subtle shadow on hover
    if (conf_hovered)
    {
        draw->shadow_circle(window->DrawList, conf_btn_rect.GetCenter(), SCALE(17),
            draw->get_clr(ImVec4(0, 0, 0, 0.15f)), SCALE(4), ImVec2(0, SCALE(2)),
            draw_flags_round_corners_all);
    }
    
    // Радиальный градиент для круглой кнопки
    draw->radial_gradient(window->DrawList, conf_btn_rect.GetCenter(), SCALE(17), conf_col_1, conf_col_2);
    
    // Анимированная подсветка
    if (state->conf_glow_animation > 0.01f)
    {
        float time = ImGui::GetTime();
        float glow_intensity = sinf(time * 2.0f) * 0.3f + 0.7f;
        ImU32 glow_color = IM_COL32(255, 255, 255, (int)(30 * state->conf_glow_animation * glow_intensity));
        draw->radial_gradient(window->DrawList, conf_btn_rect.GetCenter(), SCALE(17), glow_color, IM_COL32(255, 255, 255, 0));
    }
    
    // Icon - always white for contrast
    draw->text_clipped(window->DrawList, font->get(icons_data, 14),
        conf_btn_rect.Min, conf_btn_rect.Max,
        draw->get_clr(clr->button.text), "V", nullptr, nullptr, ImVec2(0.5f, 0.5f));
        
    gui->item_size(conf_btn_rect);

    if (create_clicked)
    {
        app_state->show_create_chat_dialog = true;
    }

    // Анимация подсветки для кнопки создания чата
    float create_target_glow = create_hovered ? 1.0f : 0.0f;
    gui->easing(state->create_glow_animation, create_target_glow, 15.f, dynamic_easing);

    // Градиентные цвета для кнопки создания чата
    ImVec4 create_grad_color_1 = clr->main.accent.Value;
    ImVec4 create_grad_color_2 = clr->main.grad_1.Value;
    
    if (create_hovered)
    {
        create_grad_color_1 = ImLerp(create_grad_color_1, ImVec4(create_grad_color_1.x * 1.2f, create_grad_color_1.y * 1.2f, create_grad_color_1.z * 1.2f, create_grad_color_1.w), state->create_glow_animation);
        create_grad_color_2 = ImLerp(create_grad_color_2, ImVec4(create_grad_color_2.x * 1.2f, create_grad_color_2.y * 1.2f, create_grad_color_2.z * 1.2f, create_grad_color_2.w), state->create_glow_animation);
    }

    ImU32 create_col_1 = draw->get_clr(create_grad_color_1);
    ImU32 create_col_2 = draw->get_clr(create_grad_color_2);
    
    // Subtle shadow on hover
    if (create_hovered)
    {
        draw->shadow_circle(window->DrawList, create_btn_rect.GetCenter(), SCALE(17),
            draw->get_clr(ImVec4(0, 0, 0, 0.15f)), SCALE(4), ImVec2(0, SCALE(2)),
            draw_flags_round_corners_all);
    }
    
    // Радиальный градиент для круглой кнопки
    draw->radial_gradient(window->DrawList, create_btn_rect.GetCenter(), SCALE(17), create_col_1, create_col_2);
    
    // Анимированная подсветка
    if (state->create_glow_animation > 0.01f)
    {
        float time = ImGui::GetTime();
        float glow_intensity = sinf(time * 2.0f) * 0.3f + 0.7f;
        ImU32 glow_color = IM_COL32(255, 255, 255, (int)(30 * state->create_glow_animation * glow_intensity));
        draw->radial_gradient(window->DrawList, create_btn_rect.GetCenter(), SCALE(17), glow_color, IM_COL32(255, 255, 255, 0));
    }

    // Icon - always white for contrast
    ImU32 icon_color = draw->get_clr(clr->button.text);
    
    draw->text_clipped(window->DrawList, font->get(icons_data, 16),
        create_btn_rect.Min, create_btn_rect.Max,
        icon_color, "P", nullptr, nullptr, ImVec2(0.5f, 0.5f)); // P=Plus

    // Регистрируем размер кнопки
    gui->item_size(create_btn_rect);

    // Поиск (упрощенная версия)
    gui->set_pos(panel_pos + SCALE(20, 70), pos_all);
    
    // Список чатов
    float list_top = SCALE(130);
    float list_height = screen_size.y - list_top;
    
    gui->set_pos(panel_pos + SCALE(0, list_top), pos_all);
    gui->begin_content("chat_list", ImVec2(panel_width, list_height), SCALE(0, 0), SCALE(0, 0));
    {
        for (auto& pair : app_state->chats)
        {
            Chat& chat = pair.second;
            bool is_selected = (chat.id == app_state->selected_chat_id);

            if (render_chat_item(chat, is_selected))
            {
                chat_manager->SwitchToChat(chat.id);
            }
        }
    }
    gui->end_content();
}

// ============================================================================
// ЦЕНТРАЛЬНАЯ ПАНЕЛЬ - СООБЩЕНИЯ
// ============================================================================
void c_messenger_widgets::render_center_panel()
{
    struct anim_state
    {
        float send_glow_animation{ 0.0f };  // Анимация подсветки для кнопки SEND
    };

    ImGuiWindow* window = gui->get_window();
    if (window->SkipItems) return;

    const ImGuiID id = window->GetID("center_panel");
    anim_state* state = gui->anim_container<anim_state>(id);

    ImVec2 screen_size = SCALE(var->window.size);
    
    float t = ImClamp(app_state->auth_to_main_progress, 0.0f, 1.0f);
    float width_factor = 0.6f + 0.4f * t;
    float left_width = SCALE(app_state->left_panel_width * width_factor);
    
    // Right panel is now a modal, so center panel takes remaining width
    float right_width = 0.0f;
    
    ImVec2 panel_pos = ImVec2(left_width, 0);
    ImVec2 panel_size = ImVec2(screen_size.x - left_width - right_width, screen_size.y);

    // Фон панели
    draw->rect_filled(window->DrawList, panel_pos, panel_pos + panel_size,
        draw->get_clr(clr->window.background), 0);

    // Если чат не выбран
    if (app_state->selected_chat_id < 0)
    {
        draw->text_clipped(window->DrawList, font->get(inter_medium_data, 16),
            panel_pos, panel_pos + panel_size,
            draw->get_clr(clr->main.text_inactive), 
            "Select a chat to start messaging", nullptr, nullptr, ImVec2(0.5f, 0.5f));
        return;
    }

    Chat* chat = app_state->GetChat(app_state->selected_chat_id);
    if (!chat)
    {
        // Чат не найден — просто ничего не рисуем в центре
        return;
    }

    // Заголовок чата
    float header_height = SCALE(70);
    ImVec2 header_pos = panel_pos;
    ImVec2 header_size = ImVec2(panel_size.x, header_height);

    // Проверяем клик по заголовку для toggle правой панели
    ImRect header_rect(header_pos, header_pos + header_size);
    bool header_hovered = header_rect.Contains(gui->mouse_pos());
    bool header_clicked = header_hovered && gui->mouse_clicked(mouse_button_left);

    if (header_clicked)
    {
        app_state->show_right_panel = !app_state->show_right_panel;
    }

    // Modern header with subtle hover effect
    ImU32 header_bg = header_hovered ? 
        draw->get_clr(clr->main.hover) : 
        draw->get_clr(clr->top_bar.background);
    
    draw->rect_filled(window->DrawList, header_pos, header_pos + header_size,
        header_bg, 0);

    // Modern divider - subtle border
    draw->rect_filled(window->DrawList,
        ImVec2(header_pos.x, header_pos.y + header_height - SCALE(1)),
        ImVec2(header_pos.x + header_size.x, header_pos.y + header_height),
        draw->get_clr(clr->main.divider), 0);

    // Имя чата
    std::string chat_name = chat_manager->GetChatDisplayName(*chat);
    draw->text_clipped(window->DrawList, font->get(inter_bold_data, 16),
        header_pos + SCALE(20, 15), header_pos + SCALE(panel_size.x - 60, 35),
        draw->get_clr(clr->main.text), chat_name.c_str(), nullptr, nullptr, ImVec2(0.0f, 0.5f));

    // Иконка info (показывает, что можно кликнуть)
    ImVec2 info_icon_pos = header_pos + SCALE(panel_size.x - 40, 20);
    ImRect info_icon_rect(info_icon_pos, info_icon_pos + SCALE(30, 30));
    
    // Modern icon with smooth color transition
    ImU32 icon_color = header_hovered ? 
        draw->get_clr(clr->main.accent) : 
        draw->get_clr(clr->main.text_secondary);
    
    draw->text_clipped(window->DrawList, font->get(icons_data, 18),
        info_icon_rect.Min, info_icon_rect.Max,
        icon_color, "I", nullptr, nullptr, ImVec2(0.5f, 0.5f)); // I=Info
    
    gui->item_size(info_icon_rect); // Добавляем регистрацию элемента

    // Информация о чате (количество участников или статус)
    std::string chat_info;
    if (chat->type == ChatType::Group)
    {
        chat_info = std::to_string(chat->participant_ids.size()) + " members";
    }
    else
    {
        // Для личного чата показываем статус
        for (int user_id : chat->participant_ids)
        {
            if (user_id != 0)
            {
                User* user = app_state->GetUser(user_id);
                if (user)
                {
                    chat_info = chat_manager->GetUserStatusString(*user);
                }
                break;
            }
        }
    }

    draw->text_clipped(window->DrawList, font->get(inter_medium_data, 12),
        header_pos + SCALE(20, 40), header_pos + SCALE(panel_size.x - 60, 60),
        draw->get_clr(clr->main.text_secondary), chat_info.c_str(), nullptr, nullptr, ImVec2(0.0f, 0.5f));

    // Область сообщений
    float input_height = SCALE(80);
    ImVec2 messages_pos = ImVec2(panel_pos.x, panel_pos.y + header_height);
    ImVec2 messages_size = ImVec2(panel_size.x, panel_size.y - header_height - input_height);

    gui->set_pos(messages_pos, pos_all);
    gui->begin_content("messages_area", messages_size, SCALE(20, 20), SCALE(0, 10));
    {
        for (const Message& msg : chat->messages)
        {
            User* sender = app_state->GetUser(msg.sender_id);
            render_message(msg, sender);
        }
    }
    gui->end_content();

    // Панель ввода сообщения
    ImVec2 input_pos = ImVec2(panel_pos.x, panel_pos.y + panel_size.y - input_height);
    ImVec2 input_size = ImVec2(panel_size.x, input_height);

    // Modern input panel background
    draw->rect_filled(window->DrawList, input_pos, input_pos + input_size,
        draw->get_clr(clr->top_bar.background), 0);

    // Modern divider - subtle top border
    draw->rect_filled(window->DrawList,
        ImVec2(input_pos.x, input_pos.y),
        ImVec2(input_pos.x + input_size.x, input_pos.y + SCALE(1)),
        draw->get_clr(clr->main.divider), 0);

    // Параметры элементов
    float element_height = SCALE(48);
    float vertical_padding = (input_height - element_height) / 2.0f;
    float side_padding = SCALE(20);
    float btn_width = SCALE(90);
    float spacing = SCALE(15);
    float input_field_width = input_size.x - (side_padding * 2) - btn_width - spacing;

    // Поле ввода
    gui->set_pos(input_pos + ImVec2(side_padding, vertical_padding), pos_all);
    widgets->text_field("Type a message", "M", app_state->message_input, sizeof(app_state->message_input), nullptr, ImVec2(input_field_width, element_height));

    // Кнопка отправки
    ImVec2 send_btn_pos = ImVec2(input_pos.x + input_size.x - side_padding - btn_width, input_pos.y + vertical_padding);
    ImRect send_btn_rect(send_btn_pos, send_btn_pos + ImVec2(btn_width, element_height));
    bool send_hovered = send_btn_rect.Contains(gui->mouse_pos());
    bool send_clicked = send_hovered && gui->mouse_clicked(mouse_button_left);

    if (send_clicked && strlen(app_state->message_input) > 0)
    {
        chat_manager->AddMessage(chat->id, 0, app_state->message_input, true);
        memset(app_state->message_input, 0, sizeof(app_state->message_input));
    }

    // Анимация подсветки при наведении
    float target_glow = send_hovered ? 1.0f : 0.0f;
    gui->easing(state->send_glow_animation, target_glow, 15.f, dynamic_easing);

    // Градиентные цвета для кнопки
    ImVec4 grad_color_1 = clr->main.accent.Value;
    ImVec4 grad_color_2 = clr->main.grad_1.Value;
    
    // Усиливаем яркость при наведении
    if (send_hovered)
    {
        grad_color_1 = ImLerp(grad_color_1, ImVec4(grad_color_1.x * 1.2f, grad_color_1.y * 1.2f, grad_color_1.z * 1.2f, grad_color_1.w), state->send_glow_animation);
        grad_color_2 = ImLerp(grad_color_2, ImVec4(grad_color_2.x * 1.2f, grad_color_2.y * 1.2f, grad_color_2.z * 1.2f, grad_color_2.w), state->send_glow_animation);
    }

    ImU32 grad_col_1 = draw->get_clr(grad_color_1);
    ImU32 grad_col_2 = draw->get_clr(grad_color_2);
    
    // Subtle shadow on hover
    if (send_hovered)
    {
        draw->shadow_rect(window->DrawList, send_btn_rect.Min, send_btn_rect.Max,
            draw->get_clr(ImVec4(0, 0, 0, 0.15f)), SCALE(6), ImVec2(0, SCALE(2)),
            draw_flags_round_corners_all, SCALE(8));
    }
    
    // Градиентная кнопка (слева направо)
    draw->rect_filled_multi_color(window->DrawList, send_btn_rect.Min, send_btn_rect.Max,
        grad_col_1, grad_col_2, grad_col_2, grad_col_1, SCALE(8));

    // Анимированная подсветка с градиентом
    if (state->send_glow_animation > 0.01f)
    {
        float time = ImGui::GetTime();
        float glow_offset = sinf(time * 2.0f) * 0.3f + 0.7f;  // Плавное движение подсветки
        
        ImVec4 glow_color_1 = ImVec4(1.0f, 1.0f, 1.0f, 0.15f * state->send_glow_animation * glow_offset);
        ImVec4 glow_color_2 = ImVec4(1.0f, 1.0f, 1.0f, 0.05f * state->send_glow_animation * glow_offset);
        
        ImU32 glow_col_1 = draw->get_clr(glow_color_1);
        ImU32 glow_col_2 = draw->get_clr(glow_color_2);
        
        // Подсветка поверх кнопки
        draw->rect_filled_multi_color(window->DrawList, send_btn_rect.Min, send_btn_rect.Max,
            glow_col_1, glow_col_2, glow_col_2, glow_col_1, SCALE(8));
    }

    // Button text - always white for primary buttons
    ImU32 send_text_color = draw->get_clr(clr->button.text);
    
    draw->text_clipped(window->DrawList, font->get(inter_bold_data, 13),
        send_btn_rect.Min, send_btn_rect.Max,
        send_text_color, "SEND", nullptr, nullptr, ImVec2(0.5f, 0.5f));

    // Регистрируем размер кнопки
    gui->item_size(send_btn_rect);
}

// ============================================================================
// ПРАВАЯ ПАНЕЛЬ - ИНФОРМАЦИЯ О ЧАТЕ
// ============================================================================
// ============================================================================
// ДИАЛОГ ИНФОРМАЦИИ О ЧАТЕ (Popup)
// ============================================================================
void c_messenger_widgets::render_chat_info_dialog()
{
    // Анимация появления/исчезновения
    float target_alpha = app_state->show_right_panel ? 1.0f : 0.0f;
    float target_offset = app_state->show_right_panel ? 0.0f : 100.0f;
    
    gui->easing(app_state->chat_info_anim_alpha, target_alpha, 10.f, dynamic_easing);
    gui->easing(app_state->chat_info_anim_offset, target_offset, 12.f, dynamic_easing);
    
    if (app_state->chat_info_anim_alpha < 0.01f && !app_state->show_right_panel)
        return;

    ImGuiWindow* main_window = gui->get_window();
    ImVec2 screen_size = SCALE(var->window.size);

    // Создаем отдельное окно-оверлей поверх всего
    ImGui::SetCursorPos(ImVec2(0, 0)); 
    
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0)); 
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0,0,0,0)); 

    if (ImGui::BeginChild("##chat_info_overlay", screen_size, false, 
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize))
    {
        ImGuiWindow* window = gui->get_window(); 

        // 1. Затемнение фона
        ImU32 overlay_color = IM_COL32(0, 0, 0, (int)(180 * app_state->chat_info_anim_alpha));
        draw->rect_filled(window->DrawList, ImVec2(0, 0), screen_size, overlay_color, 0);

        // 2. Параметры диалога
        ImVec2 dialog_size = SCALE(420, 500);
        ImVec2 dialog_pos = ImVec2(
            (screen_size.x - dialog_size.x) / 2, 
            (screen_size.y - dialog_size.y) / 2 + SCALE(app_state->chat_info_anim_offset)
        );
        ImRect dialog_rect(dialog_pos, dialog_pos + dialog_size);

        // Закрытие при клике ВНЕ области диалога
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
             if (app_state->show_right_panel && app_state->chat_info_anim_alpha > 0.99f)
             {
                 if (!dialog_rect.Contains(gui->mouse_pos()))
                 {
                    app_state->show_right_panel = false;
                 }
             }
        }

        // Фон диалога
        ImU32 dialog_bg = draw->get_clr(clr->messages.background, app_state->chat_info_anim_alpha);
        draw->rect_filled(window->DrawList, dialog_pos, dialog_pos + dialog_size, dialog_bg, SCALE(16));
        
        // Бордер
        draw->rect(window->DrawList, dialog_pos, dialog_pos + dialog_size, 
            draw->get_clr(clr->main.outline, app_state->chat_info_anim_alpha * 0.5f), SCALE(16), 0, SCALE(1));

        float alpha = app_state->chat_info_anim_alpha;
        ImVec2 mouse_pos = gui->mouse_pos();

        // Заголовок
        draw->text_clipped(window->DrawList, font->get(inter_bold_data, 20),
            dialog_pos + SCALE(30, 30), dialog_pos + SCALE(dialog_size.x - 60, 60),
            draw->get_clr(clr->main.text, alpha), "Chat Information", nullptr, nullptr, ImVec2(0.0f, 0.5f));

        // Кнопка закрытия (X)
        ImRect close_btn_rect(dialog_pos + SCALE(dialog_size.x - 50, 25), dialog_pos + SCALE(dialog_size.x - 20, 65));
        bool close_hovered = close_btn_rect.Contains(mouse_pos) && app_state->show_right_panel;
        bool close_clicked = close_hovered && gui->mouse_clicked(mouse_button_left);

        if (close_clicked)
        {
            app_state->show_right_panel = false;
        }

        draw->text_clipped(window->DrawList, font->get(icons_data, 18),
            close_btn_rect.Min, close_btn_rect.Max,
            draw->get_clr(close_hovered ? clr->main.accent : clr->main.text_inactive, alpha),
            "X", nullptr, nullptr, ImVec2(0.5f, 0.5f));

        Chat* chat = app_state->GetChat(app_state->selected_chat_id);
        if (chat)
        {
            // Аватар в центре диалога
            float avatar_size = SCALE(80);
            ImVec2 avatar_pos = dialog_pos + ImVec2((dialog_size.x - avatar_size) * 0.5f, SCALE(90));
            std::string chat_name = chat_manager->GetChatDisplayName(*chat);
            std::string initials = !chat_name.empty() ? chat_name.substr(0, 1) : "?";
            
            render_avatar(avatar_pos, avatar_size * 0.5f, initials.c_str(), draw->get_clr(clr->main.accent, alpha));
            
            // Название чата под аватаром
            draw->text_clipped(window->DrawList, font->get(inter_bold_data, 18),
                dialog_pos + SCALE(20, 180), dialog_pos + SCALE(dialog_size.x - 20, 210),
                draw->get_clr(clr->main.text, alpha), chat_name.c_str(), nullptr, nullptr, ImVec2(0.5f, 0.5f));

            // Тип чата
            const char* type_str = (chat->type == ChatType::Group) ? "Group Chat" : "Personal Chat";
            draw->text_clipped(window->DrawList, font->get(inter_medium_data, 13),
                dialog_pos + SCALE(20, 215), dialog_pos + SCALE(dialog_size.x - 20, 235),
                draw->get_clr(clr->main.text_inactive, alpha), type_str, nullptr, nullptr, ImVec2(0.5f, 0.5f));

            // Разделитель
            draw->rect_filled(window->DrawList, 
                dialog_pos + SCALE(40, 250), 
                dialog_pos + SCALE(dialog_size.x - 40, 251), 
                draw->get_clr(clr->main.outline, alpha * 0.3f), 0);

            // Список участников
            draw->text_clipped(window->DrawList, font->get(inter_bold_data, 14),
                dialog_pos + SCALE(40, 270), dialog_pos + SCALE(200, 290),
                draw->get_clr(clr->main.text, alpha), "Members", nullptr, nullptr, ImVec2(0.0f, 0.5f));

            gui->set_pos(dialog_pos + SCALE(40, 300), pos_all);
            gui->begin_content("modal_members_list", SCALE(340, 160), SCALE(0, 0), SCALE(0, 5));
            {
                for (int user_id : chat->participant_ids)
                {
                    User* user = app_state->GetUser(user_id);
                    if (!user && user_id == 0) user = &app_state->current_user;

                    if (user)
                    {
                        ImVec2 cur = gui->get_window()->DC.CursorPos;
                        ImRect item_rect(cur, cur + SCALE(340, 40));
                        
                        // Фон элемента списка
                        bool item_hovered = item_rect.Contains(mouse_pos);
                        if (item_hovered)
                        {
                            draw->rect_filled(gui->get_window()->DrawList, item_rect.Min, item_rect.Max, 
                                draw->get_clr(clr->main.hover, alpha * 0.5f), SCALE(6));
                        }

                        // Маленький аватар участника
                        render_avatar(cur + SCALE(5, 5), SCALE(15), user->name.substr(0, 1).c_str(), 
                            draw->get_clr(clr->main.outline, alpha));

                        // Имя участника
                        draw->text_clipped(gui->get_window()->DrawList, font->get(inter_medium_data, 13),
                            cur + SCALE(45, 0), cur + SCALE(340, 40),
                            draw->get_clr(clr->main.text, alpha), user->name.c_str(), nullptr, nullptr, ImVec2(0.0f, 0.5f));

                        // Статус (Online/Offline)
                        ImU32 status_col = (user->status == UserStatus::Online) ? IM_COL32(0, 255, 0, (int)(255 * alpha)) : IM_COL32(150, 150, 150, (int)(255 * alpha));
                        draw->circle_filled(gui->get_window()->DrawList, cur + SCALE(320, 20), SCALE(4), status_col, 12);

                        gui->item_size(item_rect);
                    }
                }
            }
            gui->end_content();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

// ============================================================================
// ВИДЖЕТ ЭЛЕМЕНТА ЧАТА - Modernized with professional styling
// ============================================================================
bool c_messenger_widgets::render_chat_item(const Chat& chat, bool is_selected)
{
    struct anim_state
    {
        ImVec4 bg_color{ clr->window.background };
        float alpha{ 0 };
    };

    ImGuiWindow* window = gui->get_window();
    if (window->SkipItems) return false;

    std::string id_str = "chat_item_" + std::to_string(chat.id);
    const ImGuiID id = window->GetID(id_str.c_str());
    anim_state* state = gui->anim_container<anim_state>(id);

    ImVec2 pos = window->DC.CursorPos;
    ImVec2 size = SCALE(app_state->left_panel_width, 72); // Slightly more compact: 72px height
    ImRect total(pos, pos + size);

    bool hovered = total.Contains(gui->mouse_pos());
    bool clicked = hovered && gui->mouse_clicked(mouse_button_left);

    // Modern color transitions
    ImVec4 target_bg;
    float target_alpha;
    if (is_selected)
    {
        target_bg = clr->main.selected.Value;
        target_alpha = 1.0f;
    }
    else if (hovered)
    {
        target_bg = clr->main.hover.Value;
        target_alpha = 1.0f;
    }
    else
    {
        target_bg = clr->messages.background.Value;
        target_alpha = 1.0f;
    }

    gui->easing(state->bg_color, target_bg, 12.f, dynamic_easing);
    gui->easing(state->alpha, target_alpha, 8.f, static_easing);

    // Background with modern rounded corners
    ImU32 bg_color = draw->get_clr(state->bg_color, state->alpha);
    draw->rect_filled(window->DrawList, total.Min, total.Max, bg_color, 0);

    // Left accent border for selected item (modern indicator)
    if (is_selected)
    {
        draw->rect_filled(window->DrawList,
            total.Min,
            ImVec2(total.Min.x + SCALE(3), total.Max.y),
            draw->get_clr(clr->main.accent), 0);
    }

    // Avatar - Modern circular design with compact sizing
    std::string chat_name = chat_manager->GetChatDisplayName(chat);
    std::string initials;
    if (!chat_name.empty())
        initials = chat_name.substr(0, 1);
    else
        initials = "?"; // Fallback если имя пустое

    ImVec2 avatar_pos = total.Min + SCALE(16, 18);
    float avatar_size = SCALE(28); // Compact size: 28px (reduced from 40px)
    
    // Проверка безопасности перед отрисовкой
    if (avatar_size > 0.0f && !initials.empty())
    {
        // Modern avatar with subtle border
    ImU32 avatar_color = draw->get_clr(clr->main.accent);
        // render_avatar принимает позицию верхнего левого угла квадрата и радиус
        float avatar_radius = avatar_size * 0.5f;
        render_avatar(avatar_pos, avatar_radius, initials.c_str(), avatar_color);

        // Subtle border around avatar
        ImVec2 avatar_center = avatar_pos + ImVec2(avatar_radius, avatar_radius);
        draw->circle(window->DrawList, avatar_center,
            avatar_radius, draw->get_clr(clr->main.outline, 0.2f), 32, SCALE(1));
    }

    // Chat name - Modern typography with better hierarchy
    ImVec2 name_pos = total.Min + SCALE(56, 14); // Adjusted spacing: 12px from avatar (reduced from 16px)
    ImU32 name_color = is_selected ? 
        draw->get_clr(clr->main.text) : 
        draw->get_clr(clr->main.text);
    
    draw->text_clipped(window->DrawList, font->get(inter_bold_data, 15),
        name_pos, name_pos + ImVec2(total.GetWidth() - SCALE(80), SCALE(20)),
        name_color, chat_name.c_str(), nullptr, nullptr, ImVec2(0.0f, 0.0f));

    // Last message preview - Modern secondary text
    std::string last_msg = chat_manager->GetLastMessagePreview(chat);
    ImVec2 msg_pos = total.Min + SCALE(68, 36);
    draw->text_clipped(window->DrawList, font->get(inter_medium_data, 13),
        msg_pos, msg_pos + ImVec2(total.GetWidth() - SCALE(80), SCALE(18)),
        draw->get_clr(clr->main.text_secondary), last_msg.c_str(), nullptr, nullptr, ImVec2(0.0f, 0.0f));

    // Timestamp - Modern positioning
    Message* last_message = const_cast<Chat&>(chat).GetLastMessage();
    if (last_message)
    {
        std::string time_str = chat_manager->GetFormattedTime(last_message->timestamp);
        ImVec2 time_pos = ImVec2(total.Max.x - SCALE(16), total.Min.y + SCALE(14));
        draw->text_clipped(window->DrawList, font->get(inter_medium_data, 11),
            time_pos - ImVec2(SCALE(60), 0), time_pos + ImVec2(0, SCALE(18)),
            draw->get_clr(clr->main.text_muted), time_str.c_str(), nullptr, nullptr, ImVec2(1.0f, 0.0f));
    }

    // Unread badge - Modern pill-shaped design
    if (chat.unread_count > 0)
    {
        ImVec2 badge_pos = ImVec2(total.Max.x - SCALE(16), total.Min.y + SCALE(38));
        float badge_height = SCALE(20);
        
        // Calculate badge width based on count
        std::string count_str = (chat.unread_count > 99) ? "99+" : std::to_string(chat.unread_count);
        ImVec2 badge_text_size = gui->text_size(font->get(inter_bold_data, 11), count_str.c_str());
        float badge_width = ImMax(badge_text_size.x + SCALE(12), badge_height); // Pill shape
        
        ImRect badge_rect(ImVec2(badge_pos.x - badge_width, badge_pos.y - badge_height * 0.5f),
                         ImVec2(badge_pos.x, badge_pos.y + badge_height * 0.5f));

        // Modern badge with accent color
        ImU32 badge_bg = draw->get_clr(clr->main.accent);
        draw->rect_filled(window->DrawList, badge_rect.Min, badge_rect.Max, badge_bg, badge_height * 0.5f);
        
        // Badge text - always white for contrast
        draw->text_clipped(window->DrawList, font->get(inter_bold_data, 11),
            badge_rect.Min, badge_rect.Max,
            draw->get_clr(clr->messages.bubble_text_own), count_str.c_str(), nullptr, nullptr, ImVec2(0.5f, 0.5f));
    }

    // Register item size with modern spacing
    gui->item_size(total);
    return clicked;
}

// ============================================================================
// ВИДЖЕТ СООБЩЕНИЯ - Modernized with professional styling
// ============================================================================
void c_messenger_widgets::render_message(const Message& msg, const User* sender)
{
    ImGuiWindow* window = gui->get_window();
    if (window->SkipItems) return;

    ImVec2 pos = window->DC.CursorPos;
    
    // Получаем доступную ширину из content area
    ImVec2 content_region = window->ContentRegionRect.GetSize();
    float available_width = content_region.x - SCALE(48); // Modern spacing: 24px on each side
    
    // Максимальная ширина bubble - 65% от доступной ширины (more compact, modern)
    float max_bubble_width = available_width * 0.65f;
    
    // Вычисляем размер текста с учетом переноса
    ImFont* text_font = font->get(inter_medium_data, 14); // Slightly larger for readability
    // Защита от nullptr, если по какой‑то причине шрифт не загружен
    if (!text_font)
        text_font = ImGui::GetFont();
    if (!text_font)
        return; // безопасный выход, чтобы не крашиться
    float text_padding = SCALE(16); // Modern 16px padding
    ImVec2 text_size = text_font->CalcTextSizeA(text_font->FontSize, max_bubble_width - (text_padding * 2), 0.0f, msg.text.c_str());
    
    // Ширина bubble с отступами (минимум для времени)
    float bubble_width = ImMax(text_size.x + (text_padding * 2) + SCALE(60), SCALE(100)); // Space for timestamp
    bubble_width = ImMin(bubble_width, max_bubble_width);
    
    // Учитываем имя отправителя для чужих сообщений
    float name_height = (!msg.is_own && sender) ? SCALE(22) : 0;
    float bubble_height = text_size.y + SCALE(40) + name_height; // Modern padding: 12px top, 12px bottom, 16px for timestamp

    ImVec2 bubble_pos;
    ImU32 bubble_color;
    ImU32 bubble_text_color;
    bool is_own = msg.is_own;

    if (is_own)
    {
        // Own message - right aligned, accent color with modern gradient
        bubble_pos = ImVec2(pos.x + available_width - bubble_width, pos.y);
        bubble_color = draw->get_clr(clr->messages.bubble_own);
        bubble_text_color = draw->get_clr(clr->messages.bubble_text_own);
    }
    else
    {
        // Other message - left aligned, subtle background
        bubble_pos = pos;
        bubble_color = draw->get_clr(clr->messages.bubble_other);
        bubble_text_color = draw->get_clr(clr->messages.bubble_text_other);
    }

    ImRect bubble_rect(bubble_pos, bubble_pos + ImVec2(bubble_width, bubble_height));

    // Modern rounded corners - 16px for larger bubbles, 12px for smaller
    float rounding = SCALE(16);
    if (bubble_width < SCALE(150)) rounding = SCALE(12);

    // Subtle shadow for depth (only for own messages for emphasis)
    if (is_own)
    {
        ImVec2 shadow_offset = ImVec2(0, SCALE(2));
        draw->shadow_rect(window->DrawList, bubble_rect.Min, bubble_rect.Max,
            draw->get_clr(ImVec4(0, 0, 0, 0.15f)), SCALE(8), shadow_offset,
            draw_flags_round_corners_all, rounding);
    }

    // Message bubble background
    draw->rect_filled(window->DrawList, bubble_rect.Min, bubble_rect.Max,
        bubble_color, rounding);

    // Subtle border for other messages (adds definition)
    if (!is_own)
    {
        draw->rect(window->DrawList, bubble_rect.Min, bubble_rect.Max,
            draw->get_clr(clr->main.outline, 0.3f), rounding, draw_flags_round_corners_all, SCALE(1));
    }
    
    // Имя отправителя (для чужих сообщений) - Modern typography
    float text_y_offset = SCALE(12);
    if (!is_own && sender && !sender->name.empty())
    {
        ImVec2 name_pos = bubble_rect.Min + ImVec2(text_padding, SCALE(10));
        draw->text_clipped(window->DrawList, font->get(inter_bold_data, 12),
            name_pos, name_pos + ImVec2(bubble_width - text_padding * 2, SCALE(18)),
            draw->get_clr(clr->main.accent), sender->name.c_str(), nullptr, nullptr, ImVec2(0.0f, 0.0f));
        text_y_offset = SCALE(28);
    }

    // Текст сообщения с переносом - Better line height
    ImVec2 text_pos = bubble_rect.Min + ImVec2(text_padding, text_y_offset);
    ImVec2 text_max = bubble_rect.Max - ImVec2(text_padding, SCALE(24)); // Space for timestamp
    
    window->DrawList->AddText(text_font, text_font->FontSize, text_pos,
        bubble_text_color, msg.text.c_str(), nullptr, text_max.x - text_pos.x);

    // Timestamp - Modern, subtle styling
    std::string time_str = chat_manager->GetFormattedTime(msg.timestamp);
    ImU32 time_color = draw->get_clr(clr->messages.bubble_meta);
    
    ImVec2 time_size = gui->text_size(font->get(inter_medium_data, 11), time_str.c_str());
    ImVec2 time_pos = bubble_rect.Max - ImVec2(time_size.x + text_padding, SCALE(12));
    
    draw->text_clipped(window->DrawList, font->get(inter_medium_data, 11),
        time_pos, bubble_rect.Max - ImVec2(text_padding, SCALE(4)),
        time_color,
        time_str.c_str(), nullptr, nullptr, ImVec2(0.0f, 0.0f));

    // Spacing between messages - Modern 8px spacing
    gui->item_size(ImRect(pos, pos + ImVec2(available_width, bubble_height + SCALE(8))));
}

// ============================================================================
// ДИАЛОГ СОЗДАНИЯ ЧАТА
// ============================================================================

// ============================================================================
// ВИДЖЕТ ВЫБОРА КОНТАКТА
// ============================================================================
bool c_messenger_widgets::render_contact_selector(const User& user, bool is_selected)
{
    ImGuiWindow* window = gui->get_window();
    if (window->SkipItems) return false;

    ImVec2 pos = window->DC.CursorPos;
    ImVec2 size = SCALE(460, 50);
    ImRect total(pos, pos + size);

    bool hovered = total.Contains(gui->mouse_pos());
    bool clicked = hovered && gui->mouse_clicked(mouse_button_left);

    // Фон
    draw->rect_filled(window->DrawList, total.Min, total.Max,
        draw->get_clr(hovered ? clr->main.outline : clr->messages.background, 0.3f),
        SCALE(6));

    // Чекбокс
    ImVec2 checkbox_pos = total.Min + SCALE(10, 15);
    float checkbox_size = SCALE(20);
    ImRect checkbox_rect(checkbox_pos, checkbox_pos + SCALE(checkbox_size, checkbox_size));

    draw->rect_filled(window->DrawList, checkbox_rect.Min, checkbox_rect.Max,
        draw->get_clr(is_selected ? clr->main.accent : clr->main.outline),
        SCALE(4));

    if (is_selected)
    {
        draw->text_clipped(window->DrawList, font->get(inter_bold_data, 14),
            checkbox_rect.Min, checkbox_rect.Max,
            draw->get_clr(clr->main.text), "✓", nullptr, nullptr, ImVec2(0.5f, 0.5f));
    }

    // Имя пользователя
    draw->text_clipped(window->DrawList, font->get(inter_medium_data, 13),
        total.Min + SCALE(45, 10), total.Max - SCALE(10, 10),
        draw->get_clr(clr->main.text), user.name.c_str(), nullptr, nullptr, ImVec2(0.0f, 0.5f));

    // Статус
    std::string status = chat_manager->GetUserStatusString(user);
    draw->text_clipped(window->DrawList, font->get(inter_medium_data, 11),
        total.Min + SCALE(45, 30), total.Max - SCALE(10, 10),
        draw->get_clr(clr->main.text_inactive), status.c_str(), nullptr, nullptr, ImVec2(0.0f, 0.5f));

    gui->item_size(total);
    return clicked;
}

// ============================================================================
// ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
// ============================================================================
void c_messenger_widgets::render_avatar(const ImVec2& pos, float radius, const char* initials, ImU32 color)
{
    ImGuiWindow* window = gui->get_window();
    if (window->SkipItems || window == nullptr) return;

    // Проверка безопасности: радиус должен быть положительным
    if (radius <= 0.0f) return;

    // Проверка безопасности: инициали должны быть валидными
    if (initials == nullptr || initials[0] == '\0') return;

    ImVec2 center = pos + ImVec2(radius, radius);

    // Рисуем круг аватара
    draw->circle_filled(window->DrawList, center, radius, color, 32);

    // Адаптивный цвет текста для аватара
    ImU32 text_color = get_contrast_text_color(color);
    
    // Вычисляем размер шрифта с проверкой минимального значения
    float font_size = radius * 0.8f;
    if (font_size < 8.0f) font_size = 8.0f; // Минимальный размер шрифта
    
    // Получаем шрифт с проверкой на nullptr
    ImFont* avatar_font = font->get(inter_bold_data, static_cast<int>(font_size));
    if (avatar_font == nullptr) return;
    
    // Рисуем текст с проверками
    draw->text_clipped(window->DrawList, avatar_font,
        pos, pos + ImVec2(radius * 2, radius * 2),
        text_color, initials, nullptr, nullptr, ImVec2(0.5f, 0.5f));
}

void c_messenger_widgets::render_status_indicator(const ImVec2& pos, float radius, UserStatus status)
{
    ImGuiWindow* window = gui->get_window();
    if (window->SkipItems) return;

    ImU32 status_color;
    switch (status)
    {
    case UserStatus::Online:
        status_color = IM_COL32(0, 255, 0, 255);
        break;
    case UserStatus::Away:
        status_color = IM_COL32(255, 165, 0, 255);
        break;
    case UserStatus::DoNotDisturb:
        status_color = IM_COL32(255, 0, 0, 255);
        break;
    case UserStatus::Offline:
    default:
        status_color = IM_COL32(128, 128, 128, 255);
        break;
    }

    draw->circle_filled(window->DrawList, pos, radius, status_color, 16);
}

// ============================================================================
// ФУНКЦИЯ РАСЧЕТА КОНТРАСТНОГО ЦВЕТА ТЕКСТА
// ============================================================================
ImU32 c_messenger_widgets::get_contrast_text_color(ImU32 bg_color)
{
    // Извлекаем компоненты цвета
    float r = ((bg_color >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f;
    float g = ((bg_color >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f;
    float b = ((bg_color >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f;

    // Вычисляем относительную яркость (luminance) по формуле WCAG
    float luminance = 0.2126f * r + 0.7152f * g + 0.0722f * b;

    // Если фон светлый (яркость > 0.5), возвращаем темный текст
    // Если фон темный, возвращаем светлый текст
    if (luminance > 0.5f)
    {
        return IM_COL32(0, 0, 0, 255); // Черный текст для светлого фона (максимальный контраст)
    }
    else
    {
        return IM_COL32(255, 255, 255, 255); // Светлый текст для темного фона
    }
}

// ============================================================================
// ДИАЛОГ СОЗДАНИЯ ЧАТА С АНИМАЦИЕЙ
// ============================================================================
void c_messenger_widgets::render_create_chat_dialog()
{
    struct anim_state
    {
        float create_glow_animation{ 0.0f };  // Анимация подсветки для кнопки CREATE CHAT
    };

    // Анимация появления/исчезновения
    float target_alpha = app_state->show_create_chat_dialog ? 1.0f : 0.0f;
    float target_offset = app_state->show_create_chat_dialog ? 0.0f : 100.0f;
    
    gui->easing(app_state->create_dialog_anim_alpha, target_alpha, 10.f, dynamic_easing);
    gui->easing(app_state->create_dialog_anim_offset, target_offset, 12.f, dynamic_easing);
    
    if (app_state->create_dialog_anim_alpha < 0.01f && !app_state->show_create_chat_dialog)
        return;

    const ImGuiID id = gui->get_window()->GetID("create_chat_dialog");
    anim_state* state = gui->anim_container<anim_state>(id);

    ImVec2 screen_size = SCALE(var->window.size);

    // Создаем отдельное окно-оверлей поверх всего
    // Используем BeginChild внутри основного окна, чтобы гарантировать видимость и перекрытие
    ImGui::SetCursorPos(ImVec2(0, 0)); // Сбрасываем курсор в начало
    
    // PushStyleVar для обнуления отступов child window
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0)); 
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0,0,0,0)); // Прозрачный фон

    if (ImGui::BeginChild("##create_chat_overlay", screen_size, false, 
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize))
    {
        ImGuiWindow* window = gui->get_window(); // Текущее окно (child)

        // 1. Затемнение фона (рисуем вручную на DrawList этого окна)
        // Alpha 200/255 -> ~0.78
        ImU32 overlay_color = IM_COL32(0, 0, 0, (int)(200 * app_state->create_dialog_anim_alpha));
        draw->rect_filled(window->DrawList, ImVec2(0, 0), screen_size, overlay_color, 0);

        // 3. Рисуем диалог (Calculated early for collision)
        ImVec2 dialog_size = SCALE(500, 520);
        ImVec2 dialog_pos = ImVec2(
            (screen_size.x - dialog_size.x) / 2, 
            (screen_size.y - dialog_size.y) / 2 + SCALE(app_state->create_dialog_anim_offset)
        );
        ImRect dialog_rect(dialog_pos, dialog_pos + dialog_size);

        // 2. Блокировка кликов и обработка закрытия
        // Вместо InvisibleButton используем проверку клика по окну, чтобы не блокировать InputText
        
        // Перехватываем клики, чтобы они не уходили в основное окно
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
             if (app_state->show_create_chat_dialog && app_state->create_dialog_anim_alpha > 0.99f)
             {
                 // Если клик ВНЕ области диалога - закрываем
                 if (!dialog_rect.Contains(gui->mouse_pos()))
                 {
                    app_state->show_create_chat_dialog = false;
                 }
             }
        }

        ImU32 dialog_bg = draw->get_clr(clr->messages.background, app_state->create_dialog_anim_alpha);
        draw->rect_filled(window->DrawList, dialog_pos, dialog_pos + dialog_size, dialog_bg, SCALE(12));

        float alpha = app_state->create_dialog_anim_alpha;
        ImVec2 mouse_pos = gui->mouse_pos();

        // Заголовок
        draw->text_clipped(window->DrawList, font->get(inter_bold_data, 18),
            dialog_pos + SCALE(20, 20), dialog_pos + SCALE(dialog_size.x - 20, 50),
            draw->get_clr(clr->main.text, alpha), "Create New Chat", nullptr, nullptr, ImVec2(0.0f, 0.5f));

        // Кнопка закрытия (X)
        ImRect close_btn_rect(dialog_pos + SCALE(dialog_size.x - 50, 15), dialog_pos + SCALE(dialog_size.x - 15, 50));
        bool close_hovered = close_btn_rect.Contains(mouse_pos) && app_state->show_create_chat_dialog;
        bool close_clicked = close_hovered && gui->mouse_clicked(mouse_button_left);

        if (close_clicked)
        {
            app_state->show_create_chat_dialog = false;
            app_state->ClearSelectedParticipants();
        }

        draw->text_clipped(window->DrawList, font->get(inter_bold_data, 18),
            close_btn_rect.Min, close_btn_rect.Max,
            draw->get_clr(close_hovered ? clr->main.accent : clr->main.text_inactive, alpha),
            "X", nullptr, nullptr, ImVec2(0.5f, 0.5f));

        // Тип чата (Personal / Group)
        ImVec2 type_pos = dialog_pos + SCALE(20, 70);
        
        // Кнопка Personal
        ImRect personal_rect(type_pos, type_pos + SCALE(100, 35));
        bool personal_hovered = personal_rect.Contains(mouse_pos) && app_state->show_create_chat_dialog;
        bool personal_clicked = personal_hovered && gui->mouse_clicked(mouse_button_left);

        if (personal_clicked)
            app_state->new_chat_type = ChatType::Personal;

        ImU32 p_bg = draw->get_clr(app_state->new_chat_type == ChatType::Personal ? clr->main.accent : clr->main.outline, alpha);
        draw->rect_filled(window->DrawList, personal_rect.Min, personal_rect.Max, p_bg, SCALE(6));

        // Исправлен контраст текста
        ImU32 p_text_col = (app_state->new_chat_type == ChatType::Personal) ? get_contrast_text_color(p_bg) : draw->get_clr(clr->main.text, alpha);
        // Применяем alpha к контрастному цвету, если нужно (обычно он opaque)
        if (app_state->new_chat_type == ChatType::Personal) p_text_col = (p_text_col & 0x00FFFFFF) | ((int)(255 * alpha) << 24);

        draw->text_clipped(window->DrawList, font->get(inter_medium_data, 12),
            personal_rect.Min, personal_rect.Max,
            p_text_col, "Personal", nullptr, nullptr, ImVec2(0.5f, 0.5f));

        // Кнопка Group
        ImRect group_rect(type_pos + SCALE(110, 0), type_pos + SCALE(210, 35));
        bool group_hovered = group_rect.Contains(mouse_pos) && app_state->show_create_chat_dialog;
        bool group_clicked = group_hovered && gui->mouse_clicked(mouse_button_left);

        if (group_clicked)
            app_state->new_chat_type = ChatType::Group;

        ImU32 g_bg = draw->get_clr(app_state->new_chat_type == ChatType::Group ? clr->main.accent : clr->main.outline, alpha);
        draw->rect_filled(window->DrawList, group_rect.Min, group_rect.Max, g_bg, SCALE(6));

        // Исправлен контраст текста
        ImU32 g_text_col = (app_state->new_chat_type == ChatType::Group) ? get_contrast_text_color(g_bg) : draw->get_clr(clr->main.text, alpha);
        if (app_state->new_chat_type == ChatType::Group) g_text_col = (g_text_col & 0x00FFFFFF) | ((int)(255 * alpha) << 24);

        draw->text_clipped(window->DrawList, font->get(inter_medium_data, 12),
            group_rect.Min, group_rect.Max,
            g_text_col, "Group", nullptr, nullptr, ImVec2(0.5f, 0.5f));

        // Название группы (только для групповых чатов)
        if (app_state->new_chat_type == ChatType::Group)
        {
            gui->set_pos(dialog_pos + SCALE(20, 120), pos_all);
            widgets->text_field("Group Name", "Q", app_state->new_chat_name, sizeof(app_state->new_chat_name)); // Q=Group
        }

        // Список контактов для выбора
        float list_y = app_state->new_chat_type == ChatType::Group ? SCALE(200) : SCALE(120);
        gui->set_pos(dialog_pos + SCALE(20, list_y), pos_all);
        
        draw->text_clipped(window->DrawList, font->get(inter_medium_data, 13),
            dialog_pos + SCALE(20, list_y), dialog_pos + SCALE(dialog_size.x - 20, list_y + 25),
            draw->get_clr(clr->main.text, alpha), "Select contacts:", nullptr, nullptr, ImVec2(0.0f, 0.5f));

        gui->set_pos(dialog_pos + SCALE(20, list_y + 30), pos_all);
        gui->begin_content("contact_list_dialog", SCALE(460, 220), SCALE(0, 0), SCALE(0, 5));
        {
            for (auto& pair : app_state->users)
            {
                User& user = pair.second;
                bool is_selected = std::find(app_state->selected_participants.begin(),
                    app_state->selected_participants.end(), user.id) != app_state->selected_participants.end();

                if (render_contact_selector(user, is_selected))
                {
                    if (is_selected)
                    {
                        app_state->selected_participants.erase(
                            std::remove(app_state->selected_participants.begin(),
                                app_state->selected_participants.end(), user.id),
                            app_state->selected_participants.end());
                    }
                    else
                    {
                        if (app_state->new_chat_type == ChatType::Personal)
                        {
                            app_state->selected_participants.clear();
                        }
                        app_state->selected_participants.push_back(user.id);
                    }
                }
            }
        }
        gui->end_content();

        // Кнопка создания
        ImVec2 create_btn_pos = dialog_pos + SCALE(20, dialog_size.y - 70);
        ImRect create_btn_rect(create_btn_pos, create_btn_pos + SCALE(460, 50));
        bool create_hovered = create_btn_rect.Contains(mouse_pos) && app_state->show_create_chat_dialog;
        bool create_clicked = create_hovered && gui->mouse_clicked(mouse_button_left);

        bool can_create = !app_state->selected_participants.empty();
        if (app_state->new_chat_type == ChatType::Group)
            can_create = can_create && strlen(app_state->new_chat_name) > 0;

        if (create_clicked && can_create)
        {
            std::string chat_name;
            if (app_state->new_chat_type == ChatType::Group)
            {
                chat_name = app_state->new_chat_name;
            }
            else
            {
                // Для личного чата используем имя собеседника
                if (!app_state->selected_participants.empty())
                {
                    User* user = app_state->GetUser(app_state->selected_participants[0]);
                    if (user && !user->name.empty())
                        chat_name = user->name;
                    else
                        chat_name = "User " + std::to_string(app_state->selected_participants[0]); // Fallback
                }
            }

            // Убеждаемся, что имя не пустое
            if (chat_name.empty())
            {
                if (app_state->new_chat_type == ChatType::Group)
                    chat_name = "New Group";
                else if (!app_state->selected_participants.empty())
                    chat_name = "Chat " + std::to_string(app_state->selected_participants[0]);
                else
                    chat_name = "New Chat";
            }

            int new_chat_id = chat_manager->CreateChat(chat_name, app_state->new_chat_type,
                app_state->selected_participants);

            app_state->show_create_chat_dialog = false;
            app_state->ClearSelectedParticipants();
            memset(app_state->new_chat_name, 0, sizeof(app_state->new_chat_name));
            
            // Проверка безопасности перед переключением
            Chat* new_chat = app_state->GetChat(new_chat_id);
            if (new_chat)
            {
            chat_manager->SwitchToChat(new_chat_id);
            }
        }

        // Анимация подсветки при наведении
        float target_glow = (create_hovered && can_create) ? 1.0f : 0.0f;
        gui->easing(state->create_glow_animation, target_glow, 15.f, dynamic_easing);

        // Градиентные цвета для кнопки
        ImVec4 grad_color_1, grad_color_2;
        if (can_create)
        {
            grad_color_1 = clr->main.accent.Value;
            grad_color_2 = clr->main.grad_1.Value;
            
            // Усиливаем яркость при наведении
            if (create_hovered)
            {
                grad_color_1 = ImLerp(grad_color_1, ImVec4(grad_color_1.x * 1.2f, grad_color_1.y * 1.2f, grad_color_1.z * 1.2f, grad_color_1.w), state->create_glow_animation);
                grad_color_2 = ImLerp(grad_color_2, ImVec4(grad_color_2.x * 1.2f, grad_color_2.y * 1.2f, grad_color_2.z * 1.2f, grad_color_2.w), state->create_glow_animation);
            }
        }
        else
        {
            grad_color_1 = clr->main.outline.Value;
            grad_color_2 = clr->main.outline.Value;
        }

        ImU32 grad_col_1 = draw->get_clr(grad_color_1, alpha);
        ImU32 grad_col_2 = draw->get_clr(grad_color_2, alpha);
        
        // Градиентная кнопка (слева направо)
        draw->rect_filled_multi_color(window->DrawList, create_btn_rect.Min, create_btn_rect.Max,
            grad_col_1, grad_col_2, grad_col_2, grad_col_1, SCALE(8));

        // Анимированная подсветка с градиентом
        if (state->create_glow_animation > 0.01f && can_create)
        {
            float time = ImGui::GetTime();
            float glow_offset = sinf(time * 2.0f) * 0.3f + 0.7f;  // Плавное движение подсветки
            
            ImVec4 glow_color_1 = ImVec4(1.0f, 1.0f, 1.0f, 0.15f * state->create_glow_animation * glow_offset * alpha);
            ImVec4 glow_color_2 = ImVec4(1.0f, 1.0f, 1.0f, 0.05f * state->create_glow_animation * glow_offset * alpha);
            
            ImU32 glow_col_1 = draw->get_clr(glow_color_1);
            ImU32 glow_col_2 = draw->get_clr(glow_color_2);
            
            // Подсветка поверх кнопки
            draw->rect_filled_multi_color(window->DrawList, create_btn_rect.Min, create_btn_rect.Max,
                glow_col_1, glow_col_2, glow_col_2, glow_col_1, SCALE(8));
        }

        // Определяем цвет текста на основе градиентного цвета
        ImU32 create_chat_text;
        if (can_create)
        {
            // Используем первый градиентный цвет для определения контрастного текста
            create_chat_text = get_contrast_text_color(grad_col_1);
        }
        else
        {
            create_chat_text = draw->get_clr(clr->main.text_inactive);
        }
        create_chat_text = (create_chat_text & 0x00FFFFFF) | ((int)(255 * alpha) << 24);
        
        draw->text_clipped(window->DrawList, font->get(inter_bold_data, 14),
            create_btn_rect.Min, create_btn_rect.Max,
            create_chat_text,
            "CREATE CHAT", nullptr, nullptr, ImVec2(0.5f, 0.5f));

        gui->item_size(create_btn_rect);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}


void c_messenger_widgets::render_profile_screen()
{
    // TODO: Implement profile screen
}
