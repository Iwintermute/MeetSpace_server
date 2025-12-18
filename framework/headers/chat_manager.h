#pragma once
#include "app_state.h"

// Менеджер для работы с чатами и сообщениями
class ChatManager
{
public:
    ChatManager();
    ~ChatManager();
    int CreateChat(const std::string& name, ChatType type, const std::vector<int>& participant_ids);
    bool AddMessage(int chat_id, int sender_id, const std::string& text, bool is_own = false);
    void SwitchToChat(int chat_id);
    void MarkChatAsRead(int chat_id);

    bool DeleteChat(int chat_id);
    bool DeleteMessage(int chat_id, int message_id);

    std::string GetFormattedTime(time_t timestamp);

    std::string GetRelativeTime(time_t timestamp);

    std::string GetChatDisplayName(const Chat& chat);

    std::string GetLastMessagePreview(const Chat& chat);


    bool Login(const std::string& phone, const std::string& password);

    bool SendVerificationCode(const std::string& phone);
    bool VerifyCode(const std::string& code);
    void UpdateProfile(const std::string& name, const std::string& status);

    std::string GetUserStatusString(const User& user);

    // Поиск чатов
    std::vector<int> SearchChats(const std::string& query);

private:
    int next_message_id;
};

inline std::unique_ptr<ChatManager> chat_manager = std::make_unique<ChatManager>();
