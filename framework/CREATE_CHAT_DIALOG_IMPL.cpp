// ============================================================================
// ДИАЛОГ СОЗДАНИЯ ЧАТА С АНИМАЦИЕЙ
// ============================================================================
void c_messenger_widgets::render_create_chat_dialog()
{
    ImGuiWindow* window = gui->get_window();
    if (window->SkipItems) return;

    ImVec2 screen_size = SCALE(var->window.size);
    
    // Анимация появления/исчезновения
    float target_alpha = app_state->show_create_chat_dialog ? 1.0f : 0.0f;
    float target_offset = app_state->show_create_chat_dialog ? 0.0f : 100.0f; // Вниз при закрытии
    
    gui->easing(app_state->create_dialog_anim_alpha, target_alpha, 10.f, dynamic_easing);
    gui->easing(app_state->create_dialog_anim_offset, target_offset, 12.f, dynamic_easing);
    
    // Если анимация закрытия завершена, не рендерим
    if (app_state->create_dialog_anim_alpha < 0.01f && !app_state->show_create_chat_dialog)
        return;
    
    // Уменьшенная высота диалога
    ImVec2 dialog_size = SCALE(500, 520); // Было 600, стало 520
    ImVec2 dialog_pos = ImVec2(
        (screen_size.x - dialog_size.x) / 2, 
        (screen_size.y - dialog_size.y) / 2 + SCALE(app_state->create_dialog_anim_offset)
    );

    // Затемнение фона с анимацией
    ImU32 overlay_color = IM_COL32(0, 0, 0, (int)(128 * app_state->create_dialog_anim_alpha));
    draw->rect_filled(window->DrawList, ImVec2(0, 0), screen_size, overlay_color, 0);
    
    // Блокировка кликов вне диалога
    ImRect dialog_rect(dialog_pos, dialog_pos + dialog_size);
    ImVec2 mouse_pos = gui->mouse_pos();
    bool clicked_outside = gui->mouse_clicked(mouse_button_left) && !dialog_rect.Contains(mouse_pos);
    
    // Закрытие при клике вне диалога
    if (clicked_outside && app_state->show_create_chat_dialog)
    {
        app_state->show_create_chat_dialog = false;
        return;
    }
    
    // Блокируем взаимодействие с фоном
    if (app_state->show_create_chat_dialog)
    {
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(screen_size);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
        ImGui::Begin("##dialog_blocker", nullptr, 
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | 
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }

    // Диалог с анимацией прозрачности
    ImU32 dialog_bg = draw->get_clr(clr->messages.background, app_state->create_dialog_anim_alpha);
    draw->rect_filled(window->DrawList, dialog_pos, dialog_pos + dialog_size, dialog_bg, SCALE(12));

    // Все элементы с учетом анимации прозрачности
    float alpha = app_state->create_dialog_anim_alpha;

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

    draw->rect_filled(window->DrawList, personal_rect.Min, personal_rect.Max,
        draw->get_clr(app_state->new_chat_type == ChatType::Personal ? clr->main.accent : clr->main.outline, alpha),
        SCALE(6));

    draw->text_clipped(window->DrawList, font->get(inter_medium_data, 12),
        personal_rect.Min, personal_rect.Max,
        draw->get_clr(clr->main.text, alpha), "Personal", nullptr, nullptr, ImVec2(0.5f, 0.5f));

    // Кнопка Group
    ImRect group_rect(type_pos + SCALE(110, 0), type_pos + SCALE(210, 35));
    bool group_hovered = group_rect.Contains(mouse_pos) && app_state->show_create_chat_dialog;
    bool group_clicked = group_hovered && gui->mouse_clicked(mouse_button_left);

    if (group_clicked)
        app_state->new_chat_type = ChatType::Group;

    draw->rect_filled(window->DrawList, group_rect.Min, group_rect.Max,
        draw->get_clr(app_state->new_chat_type == ChatType::Group ? clr->main.accent : clr->main.outline, alpha),
        SCALE(6));

    draw->text_clipped(window->DrawList, font->get(inter_medium_data, 12),
        group_rect.Min, group_rect.Max,
        draw->get_clr(clr->main.text, alpha), "Group", nullptr, nullptr, ImVec2(0.5f, 0.5f));

    // Название группы (только для групповых чатов)
    if (app_state->new_chat_type == ChatType::Group)
    {
        gui->set_pos(dialog_pos + SCALE(20, 120), pos_all);
        widgets->text_field("Group Name", "G", app_state->new_chat_name, sizeof(app_state->new_chat_name));
    }

    // Список контактов для выбора
    float list_y = app_state->new_chat_type == ChatType::Group ? SCALE(200) : SCALE(120);
    gui->set_pos(dialog_pos + SCALE(20, list_y), pos_all);
    
    draw->text_clipped(window->DrawList, font->get(inter_medium_data, 13),
        dialog_pos + SCALE(20, list_y), dialog_pos + SCALE(dialog_size.x - 20, list_y + 25),
        draw->get_clr(clr->main.text, alpha), "Select contacts:", nullptr, nullptr, ImVec2(0.0f, 0.5f));

    gui->set_pos(dialog_pos + SCALE(20, list_y + 30), pos_all);
    gui->begin_content("contact_list", SCALE(460, 220), SCALE(0, 0), SCALE(0, 5)); // Уменьшена высота с 300 до 220
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
                    // Убираем из выбранных
                    app_state->selected_participants.erase(
                        std::remove(app_state->selected_participants.begin(),
                            app_state->selected_participants.end(), user.id),
                        app_state->selected_participants.end());
                }
                else
                {
                    // Добавляем в выбранные
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
                if (user)
                    chat_name = user->name;
            }
        }

        int new_chat_id = chat_manager->CreateChat(chat_name, app_state->new_chat_type,
            app_state->selected_participants);

        app_state->show_create_chat_dialog = false;
        app_state->ClearSelectedParticipants();
        memset(app_state->new_chat_name, 0, sizeof(app_state->new_chat_name));
        chat_manager->SwitchToChat(new_chat_id);
    }

    ImU32 create_chat_bg = draw->get_clr(can_create ? (create_hovered ? clr->main.grad_1 : clr->main.accent) : clr->main.outline, alpha);
    
    draw->rect_filled(window->DrawList, create_btn_rect.Min, create_btn_rect.Max,
        create_chat_bg, SCALE(8));

    ImU32 create_chat_text = can_create ? get_contrast_text_color(create_chat_bg) : draw->get_clr(clr->main.text_inactive);
    create_chat_text = (create_chat_text & 0x00FFFFFF) | ((int)(255 * alpha) << 24); // Применяем alpha
    
    draw->text_clipped(window->DrawList, font->get(inter_bold_data, 14),
        create_btn_rect.Min, create_btn_rect.Max,
        create_chat_text,
        "CREATE CHAT", nullptr, nullptr, ImVec2(0.5f, 0.5f));

    gui->item_size(create_btn_rect);
}
