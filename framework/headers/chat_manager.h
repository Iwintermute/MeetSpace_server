#pragma once
#include "app_state.h"

// Менеджер для работы с чатами и сообщениями
class ChatManager
{
public:
    ChatManager();
    ~ChatManager();

    // Создать новый чат
    int CreateChat(const std::string& name, ChatType type, const std::vector<int>& participant_ids);

    // Добавить сообщение в чат
    bool AddMessage(int chat_id, int sender_id, const std::string& text, bool is_own = false);

    // Переключиться на чат
    void SwitchToChat(int chat_id);

    // Отметить сообщения как прочитанные
    void MarkChatAsRead(int chat_id);

    // Удалить чат
    bool DeleteChat(int chat_id);

    // Удалить сообщение
    bool DeleteMessage(int chat_id, int message_id);

    // Получить отформатированное время
    std::string GetFormattedTime(time_t timestamp);

    // Получить относительное время ("5 min ago", "2 hours ago")
    std::string GetRelativeTime(time_t timestamp);

    // Получить имя чата для отображения
    std::string GetChatDisplayName(const Chat& chat);

    // Получить превью последнего сообщения
    std::string GetLastMessagePreview(const Chat& chat);

    // Аутентификация
    bool Login(const std::string& phone, const std::string& password);

    // Регистрация - отправка кода
    bool SendVerificationCode(const std::string& phone);

    // Регистрация - проверка кода
    bool VerifyCode(const std::string& code);

    // Обновить профиль
    void UpdateProfile(const std::string& name, const std::string& status);

    // Получить статус пользователя в виде строки
    std::string GetUserStatusString(const User& user);

    // Поиск чатов
    std::vector<int> SearchChats(const std::string& query);

private:
    int next_message_id;
};

inline std::unique_ptr<ChatManager> chat_manager = std::make_unique<ChatManager>();
