#pragma once
#include "includes.h"
#include <string>
#include <vector>
#include <map>
#include <ctime>

enum class AppScreen
{
    Login,
    Registration,
    PhoneVerification,
    MainMessenger,
    Profile,
    Settings,
    Conference
};

enum class ChatType
{
    Personal,
    Group
};


enum class UserStatus
{
    Online,
    Offline,
    Away,
    DoNotDisturb
};


struct Message
{
    int id;
    int chat_id;
    int sender_id;
    std::string text;
    time_t timestamp;
    bool is_read;
    bool is_own; // Свое сообщение или чужое

    Message() : id(0), chat_id(0), sender_id(0), timestamp(0), is_read(false), is_own(false) {}
    Message(int _id, int _chat_id, int _sender_id, const std::string& _text, bool _is_own = false)
        : id(_id), chat_id(_chat_id), sender_id(_sender_id), text(_text), is_own(_is_own)
    {
        timestamp = time(nullptr);
        is_read = false;
    }
};

// Структура пользователя/контакта
struct User
{
    int id;
    std::string name;
    std::string phone;
    std::string status_text;
    UserStatus status;
    time_t last_seen;
    int avatar_id; // ID текстуры аватара (если есть)

    User() : id(0), status(UserStatus::Offline), last_seen(0), avatar_id(0) {}
    User(int _id, const std::string& _name, const std::string& _phone)
        : id(_id), name(_name), phone(_phone), status(UserStatus::Online), 
          status_text("Available"), last_seen(time(nullptr)), avatar_id(0) {}
};

// Структура чата
struct Chat
{
    int id;
    std::string name;
    ChatType type;
    std::vector<int> participant_ids;
    std::vector<Message> messages;
    time_t created_at;
    int unread_count;
    bool is_typing; 

    Chat() : id(0), type(ChatType::Personal), created_at(0), unread_count(0), is_typing(false) {}
    Chat(int _id, const std::string& _name, ChatType _type, const std::vector<int>& _participants)
        : id(_id), name(_name), type(_type), participant_ids(_participants), 
          created_at(time(nullptr)), unread_count(0), is_typing(false) {}
    Message* GetLastMessage()
    {
        if (messages.empty()) return nullptr;
        return &messages.back();
    }
};


class AppState
{
public:
    AppState();
    ~AppState();

    AppScreen current_screen;
    User current_user;
    bool is_authenticated;
    std::map<int, User> users;
    std::map<int, Chat> chats;
    int selected_chat_id;

    char phone_input[32];
    char verification_code[8];
    bool code_sent;
    std::string sent_to_phone;
    char login_phone[32];
    char login_password[64];
    bool show_create_chat_dialog;
    std::vector<int> selected_participants;
    char new_chat_name[128];
    ChatType new_chat_type;
    float create_dialog_anim_alpha;   // Анимация прозрачности диалога
    float create_dialog_anim_offset;  // Анимация смещения диалога (для slide-in/out)

    // Временные данные для профиля
    char profile_name_edit[128];
    char profile_status_edit[256];

    // Временные данные для ввода сообщения
    char message_input[2048];

    // UI состояния
    bool show_right_panel;
    float left_panel_width;
    float right_panel_width;
    float right_panel_anim_width; // Анимированная ширина правой панели

    // Анимация перехода от компактного окна авторизации к полному мессенджеру
    float auth_to_main_progress; // 0.0f -> 1.0f
    bool  auth_expanding;        // сейчас идет анимация расширения

    // Инициализация фейковых данных
    void InitializeFakeData();

    // Получить чат по ID
    Chat* GetChat(int chat_id);

    // Получить пользователя по ID
    User* GetUser(int user_id);

    // Очистить выбранных участников
    void ClearSelectedParticipants();

private:
    int next_user_id;
    int next_chat_id;
    int next_message_id;

    friend class ChatManager;
};

inline std::unique_ptr<AppState> app_state = std::make_unique<AppState>();
