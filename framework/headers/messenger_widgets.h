#pragma once
#include "includes.h"
#include "app_state.h"
#include "chat_manager.h"

class c_messenger_widgets
{
public:
    // Экраны аутентификации
    void render_login_screen();
    void render_registration_screen();
    void render_phone_verification_screen();

    // Главный интерфейс мессенджера
    void render_main_messenger();

    // Компоненты главного интерфейса
    void render_left_panel();      // Список чатов
    void render_center_panel();    // Область сообщений
    void render_right_panel();     // Информация о чате

    // Виджеты
    bool render_chat_item(const Chat& chat, bool is_selected);
    void render_message(const Message& msg, const User* sender);
    bool render_contact_selector(const User& user, bool is_selected);
    
    // Диалоги
    void render_create_chat_dialog();
    void render_profile_screen();

    // Вспомогательные функции
    void render_avatar(const ImVec2& pos, float radius, const char* initials, ImU32 color);
    void render_status_indicator(const ImVec2& pos, float radius, UserStatus status);
    ImU32 get_contrast_text_color(ImU32 bg_color);

private:
    // Временные переменные для UI
    char search_buffer[256] = { 0 };
    float chat_list_scroll = 0.0f;
    float message_list_scroll = 0.0f;
};

inline std::unique_ptr<c_messenger_widgets> messenger_widgets = std::make_unique<c_messenger_widgets>();
