#include "../headers/app_state.h"
#include <cstring>

AppState::AppState()
    : current_screen(AppScreen::Login)
    , is_authenticated(false)
    , selected_chat_id(-1)
    , code_sent(false)
    , show_create_chat_dialog(false)
    , new_chat_type(ChatType::Personal)
    , create_dialog_anim_alpha(0.0f)
    , create_dialog_anim_offset(-100.0f)
    , show_right_panel(false)
    , left_panel_width(300.0f)
    , right_panel_width(280.0f)
    , right_panel_anim_width(0.0f)
    , next_user_id(1)
    , next_chat_id(1)
    , next_message_id(1)
    , auth_to_main_progress(1.0f)
    , auth_expanding(false)
{
    memset(phone_input, 0, sizeof(phone_input));
    memset(verification_code, 0, sizeof(verification_code));
    memset(login_phone, 0, sizeof(login_phone));
    memset(login_password, 0, sizeof(login_password));
    memset(new_chat_name, 0, sizeof(new_chat_name));
    memset(profile_name_edit, 0, sizeof(profile_name_edit));
    memset(profile_status_edit, 0, sizeof(profile_status_edit));
    memset(message_input, 0, sizeof(message_input));

    InitializeFakeData();
}

AppState::~AppState()
{
}

void AppState::InitializeFakeData()
{
    // Создаем текущего пользователя
    current_user = User(0, "You", "+1234567890");
    current_user.status_text = "Available";
    current_user.status = UserStatus::Online;

    // Создаем фейковых пользователей (контакты)
    users[1] = User(1, "Alice Johnson", "+1234567891");
    users[1].status = UserStatus::Online;
    users[1].status_text = "Working from home";

    users[2] = User(2, "Bob Smith", "+1234567892");
    users[2].status = UserStatus::Away;
    users[2].status_text = "In a meeting";
    users[2].last_seen = time(nullptr) - 300; // 5 минут назад

    users[3] = User(3, "Charlie Brown", "+1234567893");
    users[3].status = UserStatus::Offline;
    users[3].status_text = "Sleeping";
    users[3].last_seen = time(nullptr) - 3600; // 1 час назад

    users[4] = User(4, "Diana Prince", "+1234567894");
    users[4].status = UserStatus::Online;
    users[4].status_text = "Available";

    users[5] = User(5, "Eve Davis", "+1234567895");
    users[5].status = UserStatus::DoNotDisturb;
    users[5].status_text = "Do not disturb";

    users[6] = User(6, "Frank Miller", "+1234567896");
    users[6].status = UserStatus::Offline;
    users[6].last_seen = time(nullptr) - 7200; // 2 часа назад

    // Создаем личные чаты
    Chat chat1(1, "Alice Johnson", ChatType::Personal, { 0, 1 });
    chat1.messages.push_back(Message(next_message_id++, 1, 1, "Hey! How are you?", false));
    chat1.messages.push_back(Message(next_message_id++, 1, 0, "I'm good, thanks! How about you?", true));
    chat1.messages.push_back(Message(next_message_id++, 1, 1, "Doing great! Want to grab coffee later?", false));
    chat1.messages.push_back(Message(next_message_id++, 1, 0, "Sure, sounds good!", true));
    chat1.messages.push_back(Message(next_message_id++, 1, 1, "Perfect! See you at 3 PM?", false));
    chat1.unread_count = 1;
    chats[1] = chat1;

    Chat chat2(2, "Bob Smith", ChatType::Personal, { 0, 2 });
    chat2.messages.push_back(Message(next_message_id++, 2, 2, "Did you finish the report?", false));
    chat2.messages.push_back(Message(next_message_id++, 2, 0, "Almost done, will send it by EOD", true));
    chat2.messages.push_back(Message(next_message_id++, 2, 2, "Great, thanks!", false));
    chats[2] = chat2;

    Chat chat3(3, "Charlie Brown", ChatType::Personal, { 0, 3 });
    chat3.messages.push_back(Message(next_message_id++, 3, 0, "Hey Charlie, long time no see!", true));
    chat3.messages.push_back(Message(next_message_id++, 3, 3, "Yeah! We should catch up soon", false));
    chats[3] = chat3;

    // Создаем групповой чат
    Chat group1(4, "Project Team", ChatType::Group, { 0, 1, 2, 4 });
    group1.messages.push_back(Message(next_message_id++, 4, 1, "Hey team! Let's discuss the new feature", false));
    group1.messages.push_back(Message(next_message_id++, 4, 2, "I think we should focus on performance first", false));
    group1.messages.push_back(Message(next_message_id++, 4, 0, "Agreed. Let's schedule a meeting", true));
    group1.messages.push_back(Message(next_message_id++, 4, 4, "I'm available tomorrow afternoon", false));
    group1.messages.push_back(Message(next_message_id++, 4, 1, "Works for me!", false));
    group1.unread_count = 2;
    chats[4] = group1;

    Chat group2(5, "Weekend Plans", ChatType::Group, { 0, 3, 5, 6 });
    group2.messages.push_back(Message(next_message_id++, 5, 3, "Anyone up for hiking this weekend?", false));
    group2.messages.push_back(Message(next_message_id++, 5, 5, "Count me in!", false));
    group2.messages.push_back(Message(next_message_id++, 5, 0, "Sounds fun! What time?", true));
    chats[5] = group2;

    next_chat_id = 6;
}

Chat* AppState::GetChat(int chat_id)
{
    auto it = chats.find(chat_id);
    if (it != chats.end())
        return &it->second;
    return nullptr;
}

User* AppState::GetUser(int user_id)
{
    auto it = users.find(user_id);
    if (it != users.end())
        return &it->second;
    return nullptr;
}

void AppState::ClearSelectedParticipants()
{
    selected_participants.clear();
}
