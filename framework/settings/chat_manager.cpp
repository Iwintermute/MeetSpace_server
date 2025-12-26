#include "../headers/chat_manager.h"
#include <sstream>
#include <iomanip>
#include <algorithm>

ChatManager::ChatManager()
    : next_message_id(1000)
{
}

ChatManager::~ChatManager()
{
}

int ChatManager::CreateChat(const std::string& name, ChatType type, const std::vector<int>& participant_ids)
{
    int new_chat_id = app_state->next_chat_id++;
    
    std::vector<int> all_participants = participant_ids;
    // Добавляем текущего пользователя, если его нет
    if (std::find(all_participants.begin(), all_participants.end(), 0) == all_participants.end())
    {
        all_participants.insert(all_participants.begin(), 0);
    }

    // ВАЖНО: Создаем пользователей, если их нет в app_state->users
    // Это предотвращает краш при попытке получить пользователя через GetUser
    for (int user_id : all_participants)
    {
        if (user_id != 0) // Пропускаем текущего пользователя (ID 0)
        {
            User* existing_user = app_state->GetUser(user_id);
            if (existing_user == nullptr)
            {
                // Создаем нового пользователя, если его нет
                std::string user_name = "User " + std::to_string(user_id);
                std::string user_phone = "+123456789" + std::to_string(user_id % 10);
                app_state->users[user_id] = User(user_id, user_name, user_phone);
                app_state->users[user_id].status = UserStatus::Online;
                app_state->users[user_id].status_text = "Available";
            }
        }
    }

    Chat new_chat(new_chat_id, name, type, all_participants);
    app_state->chats[new_chat_id] = new_chat;

    return new_chat_id;
}

bool ChatManager::AddMessage(int chat_id, int sender_id, const std::string& text, bool is_own)
{
    Chat* chat = app_state->GetChat(chat_id);
    if (!chat) return false;

    // ВАЖНО: Создаем пользователя-отправителя, если его нет (для безопасности)
    if (sender_id != 0 && !is_own) // Пропускаем текущего пользователя и свои сообщения
    {
        User* sender = app_state->GetUser(sender_id);
        if (sender == nullptr)
        {
            // Создаем пользователя, если его нет
            std::string user_name = "User " + std::to_string(sender_id);
            std::string user_phone = "+123456789" + std::to_string(sender_id % 10);
            app_state->users[sender_id] = User(sender_id, user_name, user_phone);
            app_state->users[sender_id].status = UserStatus::Online;
            app_state->users[sender_id].status_text = "Available";
        }
    }

    Message msg(next_message_id++, chat_id, sender_id, text, is_own);
    chat->messages.push_back(msg);

    // Если сообщение не наше, увеличиваем счетчик непрочитанных
    if (!is_own && chat_id != app_state->selected_chat_id)
    {
        chat->unread_count++;
    }

    return true;
}

void ChatManager::SwitchToChat(int chat_id)
{
    // Проверка безопасности: убеждаемся, что чат существует
    Chat* chat = app_state->GetChat(chat_id);
    if (!chat)
    {
        // Если чат не найден, сбрасываем selected_chat_id
        app_state->selected_chat_id = -1;
        return;
    }
    
    app_state->selected_chat_id = chat_id;
    MarkChatAsRead(chat_id);
}

void ChatManager::MarkChatAsRead(int chat_id)
{
    Chat* chat = app_state->GetChat(chat_id);
    if (!chat) return;

    chat->unread_count = 0;
    for (auto& msg : chat->messages)
    {
        if (!msg.is_own)
            msg.is_read = true;
    }
}

bool ChatManager::DeleteChat(int chat_id)
{
    auto it = app_state->chats.find(chat_id);
    if (it == app_state->chats.end()) return false;

    app_state->chats.erase(it);
    
    if (app_state->selected_chat_id == chat_id)
    {
        app_state->selected_chat_id = -1;
    }

    return true;
}

bool ChatManager::DeleteMessage(int chat_id, int message_id)
{
    Chat* chat = app_state->GetChat(chat_id);
    if (!chat) return false;

    auto it = std::find_if(chat->messages.begin(), chat->messages.end(),
        [message_id](const Message& msg) { return msg.id == message_id; });

    if (it != chat->messages.end())
    {
        chat->messages.erase(it);
        return true;
    }

    return false;
}

std::string ChatManager::GetFormattedTime(time_t timestamp)
{
    struct tm timeinfo;
    localtime_s(&timeinfo, &timestamp);
    
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << timeinfo.tm_hour << ":"
        << std::setfill('0') << std::setw(2) << timeinfo.tm_min;
    
    return oss.str();
}

std::string ChatManager::GetRelativeTime(time_t timestamp)
{
    time_t now = time(nullptr);
    int diff = static_cast<int>(now - timestamp);

    if (diff < 60)
        return "Just now";
    else if (diff < 3600)
        return std::to_string(diff / 60) + " min ago";
    else if (diff < 86400)
        return std::to_string(diff / 3600) + " hours ago";
    else if (diff < 604800)
        return std::to_string(diff / 86400) + " days ago";
    else
        return GetFormattedTime(timestamp);
}

std::string ChatManager::GetChatDisplayName(const Chat& chat)
{
    if (chat.type == ChatType::Group)
        return chat.name.empty() ? "Group Chat" : chat.name;

    // Для личного чата показываем имя собеседника
    for (int user_id : chat.participant_ids)
    {
        if (user_id != 0) // Не текущий пользователь
        {
            User* user = app_state->GetUser(user_id);
            if (user && !user->name.empty())
                return user->name;
        }
    }

    // Fallback: если пользователь не найден, используем имя чата или дефолтное
    return chat.name.empty() ? "Unknown User" : chat.name;
}

std::string ChatManager::GetLastMessagePreview(const Chat& chat)
{
    Message* last_msg = const_cast<Chat&>(chat).GetLastMessage();
    if (!last_msg)
        return "No messages yet";

    std::string preview = last_msg->text;
    if (preview.length() > 50)
        preview = preview.substr(0, 47) + "...";

    // Добавляем префикс "You: " для своих сообщений
    if (last_msg->is_own)
        preview = "You: " + preview;
    else if (chat.type == ChatType::Group)
    {
        User* sender = app_state->GetUser(last_msg->sender_id);
        if (sender && !sender->name.empty())
            preview = sender->name + ": " + preview;
        else
            preview = "User: " + preview; // Fallback если отправитель не найден
    }

    return preview;
}

bool ChatManager::Login(const std::string& phone, const std::string& password)
{
    // Фейковая логика - любой пароль подходит
    if (phone.empty())
        return false;

    app_state->current_user.phone = phone;
    app_state->is_authenticated = true;
    
    // Запускаем анимацию плавного расширения окна под мессенджер
    app_state->auth_to_main_progress = 0.0f;
    app_state->auth_expanding = true;

    app_state->current_screen = AppScreen::MainMessenger;

    return true;
}

bool ChatManager::SendVerificationCode(const std::string& phone)
{
    if (phone.empty())
        return false;

    app_state->code_sent = true;
    app_state->sent_to_phone = phone;
    
    return true;
}

bool ChatManager::VerifyCode(const std::string& code)
{
    // Фейковая проверка - код "1234" всегда правильный
    if (code == "1234" || code.length() >= 4)
    {
        app_state->current_user.phone = app_state->sent_to_phone;
        app_state->current_user.name = "New User";
        app_state->is_authenticated = true;
        
        // Запускаем анимацию плавного расширения окна под мессенджер
        app_state->auth_to_main_progress = 0.0f;
        app_state->auth_expanding = true;

        app_state->current_screen = AppScreen::MainMessenger;
        return true;
    }

    return false;
}

void ChatManager::UpdateProfile(const std::string& name, const std::string& status)
{
    if (!name.empty())
        app_state->current_user.name = name;
    
    if (!status.empty())
        app_state->current_user.status_text = status;
}

std::string ChatManager::GetUserStatusString(const User& user)
{
    switch (user.status)
    {
    case UserStatus::Online:
        return "Online";
    case UserStatus::Away:
        return "Away";
    case UserStatus::DoNotDisturb:
        return "Do not disturb";
    case UserStatus::Offline:
        return "Last seen " + GetRelativeTime(user.last_seen);
    default:
        return "Unknown";
    }
}

std::vector<int> ChatManager::SearchChats(const std::string& query)
{
    std::vector<int> results;
    
    if (query.empty())
    {
        // Возвращаем все чаты
        for (const auto& pair : app_state->chats)
        {
            results.push_back(pair.first);
        }
        return results;
    }

    std::string lower_query = query;
    std::transform(lower_query.begin(), lower_query.end(), lower_query.begin(), ::tolower);

    for (const auto& pair : app_state->chats)
    {
        std::string chat_name = GetChatDisplayName(pair.second);
        std::transform(chat_name.begin(), chat_name.end(), chat_name.begin(), ::tolower);

        if (chat_name.find(lower_query) != std::string::npos)
        {
            results.push_back(pair.first);
        }
    }

    return results;
}
