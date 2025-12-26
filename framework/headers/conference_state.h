#pragma once
#include <string>
#include <vector>
#include <map>
#include <ctime>

enum class ConferenceAccess {
    Open,
    InviteOnly,
    ApprovalRequired
};

enum class UserRole {
    Host,
    CoHost,
    Participant,
    WaitingRoom
};

enum class ConferenceStatus {
    Scheduled,
    Active,
    Ended
};

struct ConferenceSettings {
    std::string title;
    ConferenceAccess access = ConferenceAccess::Open;
    int duration_minutes = 60; // 0 = unlimited
    std::string password;
    bool auto_mute_on_join = false;
    bool auto_camera_on_join = false;
    bool auto_record = false;
    time_t start_time = 0;
    std::vector<int> invited_user_ids;
};

struct ConferenceParticipant {
    int user_id;
    UserRole role = UserRole::Participant;
    bool microphone_enabled = false;
    bool camera_enabled = false;
    bool is_speaking = false;
    bool hand_raised = false;
    std::vector<std::string> active_reactions;
    time_t joined_at = 0;
    int video_stream_id = 0; 
    int audio_stream_id = 0;
    
    // UI animation states
    float sound_level = 0.0f;     // For audio visualization (0.0 - 1.0)
    float speaking_timer = 0.0f;  // for fading out speaking border
};

struct ConferenceMessage {
    int id;
    int sender_id;
    std::string text;
    time_t timestamp;
    bool is_system = false;       // "User joined", etc.
    std::vector<std::string> reactions;
};

// Fake video stream data for demo
struct FakeVideoStream {
    float r, g, b;             // Background color
    std::string avatar_text;   // Text if camera off
    bool has_camera;           // If true, render "video", else bg color + text
    bool is_active;
};

struct Conference {
    int id;
    ConferenceSettings settings;
    int creator_id;
    std::map<int, ConferenceParticipant> participants;
    std::vector<ConferenceMessage> messages;
    bool is_recording = false;
    bool screen_sharing_active = false;
    int screen_sharing_user_id = -1;
    time_t created_at = 0;
    ConferenceStatus status = ConferenceStatus::Scheduled;
};

// UI States
enum class ConferenceUIState {
    ListView,           // Список конференций
    CreationModal,      // Создание конференции
    ActiveConference,   // Активная конференция
    Settings,           // Настройки конференции
    ChatOnly,           // Только чат конференции
    WaitingRoom,        // Комната ожидания
};
