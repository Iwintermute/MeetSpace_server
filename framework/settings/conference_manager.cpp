#include "../headers/conference_manager.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

ConferenceManager* conference_manager = new ConferenceManager();

ConferenceManager::ConferenceManager() {
    // Create an initial fake conference for demo
    ConferenceSettings s;
    s.title = "Direct Board Meeting";
    s.duration_minutes = 60;
    s.access = ConferenceAccess::Open;
    s.start_time = time(nullptr) + 3600;
    
    int id = CreateConference(s);
    
    // Add some fake participants to it
    if (conferences.find(id) != conferences.end()) {
        GenerateFakeParticipants(conferences[id], 5);
    }
}

ConferenceManager::~ConferenceManager() {
}

int ConferenceManager::CreateConference(const ConferenceSettings& settings) {
    Conference conf;
    conf.id = next_conference_id++;
    conf.settings = settings;
    conf.creator_id = current_user_id;
    conf.created_at = time(nullptr);
    conf.status = ConferenceStatus::Scheduled;
    
    // Auto-add creator
    ConferenceParticipant creator;
    creator.user_id = current_user_id;
    creator.role = UserRole::Host;
    creator.joined_at = time(nullptr);
    creator.is_speaking = false;
    // Apply settings defaults
    creator.microphone_enabled = !settings.auto_mute_on_join;
    creator.camera_enabled = !settings.auto_camera_on_join;
    
    conf.participants[current_user_id] = creator;

    // Add invited users (Simulated auto-join for demo)
    for (int uid : settings.invited_user_ids) {
        ConferenceParticipant p;
        p.user_id = uid;
        p.role = UserRole::Participant;
        p.joined_at = time(nullptr);
        p.microphone_enabled = !settings.auto_mute_on_join;
        p.camera_enabled = !settings.auto_camera_on_join;
        
        // Setup fake stream if needed (for visual consistency)
        if (fake_streams.find(uid) == fake_streams.end()) {
             FakeVideoStream fs;
             fs.r = (float)(rand() % 100) / 100.0f;
             fs.g = (float)(rand() % 100) / 100.0f;
             fs.b = (float)(rand() % 100) / 100.0f;
             fs.avatar_text = "U" + std::to_string(uid);
             fs.has_camera = p.camera_enabled;
             fs.is_active = true;
             fake_streams[uid] = fs;
        }

        conf.participants[uid] = p;
    }
    
    conferences[conf.id] = conf;
    return conf.id;
}

bool ConferenceManager::JoinConference(int conference_id, const std::string& password) {
    if (conferences.find(conference_id) == conferences.end()) return false;
    
    Conference& conf = conferences[conference_id];
    
    // Already joined?
    if (conf.participants.find(current_user_id) != conf.participants.end()) {
        current_conference_id = conference_id;
        return true;
    }
    
    // Validate password (basic check)
    if (conf.settings.access == ConferenceAccess::InviteOnly && !conf.settings.password.empty()) {
        if (conf.settings.password != password) return false;
    }
    
    ConferenceParticipant p;
    p.user_id = current_user_id;
    p.role = UserRole::Participant;
    p.joined_at = time(nullptr);
    p.microphone_enabled = !conf.settings.auto_mute_on_join;
    p.camera_enabled = !conf.settings.auto_camera_on_join;
    
    conf.participants[current_user_id] = p;
    
    // Add message
    ConferenceMessage msg;
    msg.id = rand();
    msg.sender_id = -1; // System
    msg.text = "You joined the conference";
    msg.timestamp = time(nullptr);
    msg.is_system = true;
    conf.messages.push_back(msg);
    
    current_conference_id = conference_id;
    conf.status = ConferenceStatus::Active;
    
    return true;
}

bool ConferenceManager::LeaveConference(int conference_id) {
    if (conference_id == current_conference_id) {
        current_conference_id = -1;
    }
    
    if (conferences.find(conference_id) != conferences.end()) {
        Conference& conf = conferences[conference_id];
        conf.participants.erase(current_user_id);
    }
    return true;
}

bool ConferenceManager::EndConference(int conference_id) {
    if (conferences.find(conference_id) != conferences.end()) {
        conferences[conference_id].status = ConferenceStatus::Ended;
        if (current_conference_id == conference_id) {
            current_conference_id = -1;
        }
        return true;
    }
    return false;
}

bool ConferenceManager::ToggleMicrophone(int conference_id) {
    if (conferences.find(conference_id) != conferences.end()) {
        auto& p = conferences[conference_id].participants[current_user_id];
        p.microphone_enabled = !p.microphone_enabled;
        return p.microphone_enabled;
    }
    return false;
}

bool ConferenceManager::ToggleCamera(int conference_id) {
    if (conferences.find(conference_id) != conferences.end()) {
        auto& p = conferences[conference_id].participants[current_user_id];
        p.camera_enabled = !p.camera_enabled;
        return p.camera_enabled;
    }
    return false;
}

bool ConferenceManager::ToggleRecording(int conference_id) {
    if (conferences.find(conference_id) != conferences.end()) {
        Conference& conf = conferences[conference_id];
        conf.is_recording = !conf.is_recording;
        
        // Notify
        ConferenceMessage msg;
        msg.id = rand();
        msg.sender_id = -1;
        msg.text = conf.is_recording ? "Recording started" : "Recording stopped";
        msg.timestamp = time(nullptr);
        msg.is_system = true;
        conf.messages.push_back(msg);

        return conf.is_recording;
    }
    return false;
}

void ConferenceManager::GenerateFakeParticipants(Conference& conference, int count) {
    for (int i = 0; i < count; i++) {
        ConferenceParticipant p;
        p.user_id = 1000 + i;
        if (i == 0) p.role = UserRole::Host;
        else if (i == 1) p.role = UserRole::CoHost;
        else p.role = UserRole::Participant;
        
        p.microphone_enabled = (i % 3 != 0);
        p.camera_enabled = (i % 2 == 0);
        p.is_speaking = (i == 0); // Participants 0 speaks initially
        p.hand_raised = (i == 3);
        p.joined_at = time(nullptr) - rand() % 3600;
        
        // Setup fake stream visual
        FakeVideoStream fs;
        fs.r = (float)(rand() % 100) / 100.0f;
        fs.g = (float)(rand() % 100) / 100.0f;
        fs.b = (float)(rand() % 100) / 100.0f;
        fs.avatar_text = "User " + std::to_string(i);
        fs.has_camera = p.camera_enabled;
        fs.is_active = true;
        
        fake_streams[p.user_id] = fs;
        
        conference.participants[p.user_id] = p;
    }
}

Conference* ConferenceManager::GetConference(int conference_id) {
    if (conferences.find(conference_id) != conferences.end()) {
        return &conferences[conference_id];
    }
    return nullptr;
}

std::vector<Conference> ConferenceManager::GetUserConferences(int user_id) {
    std::vector<Conference> result;
    for (const auto& pair : conferences) {
        // Return all for demo, or filter by creator/participant
        result.push_back(pair.second);
    }
    return result;
}

void ConferenceManager::SendReaction(int conference_id, const std::string& emoji) {
    if (conferences.find(conference_id) != conferences.end()) {
        auto& p = conferences[conference_id].participants[current_user_id];
        p.active_reactions.push_back(emoji);
        // Logic to remove old reactions would be needed in update loop
    }
}

void ConferenceManager::RaiseHand(int conference_id, bool raise) {
     if (conferences.find(conference_id) != conferences.end()) {
        auto& p = conferences[conference_id].participants[current_user_id];
        p.hand_raised = raise;
    }
}

void ConferenceManager::SendChatMessage(int conference_id, const std::string& text) {
    auto it = conferences.find(conference_id);
    if (it == conferences.end()) return;

    ConferenceMessage msg;
    msg.id = rand();
    msg.sender_id = current_user_id;
    msg.text = text;
    msg.timestamp = time(nullptr);
    
    it->second.messages.push_back(msg);
}
void ConferenceManager::UpdateFakeData(float delta_time) {
    if (current_conference_id == -1) return;
    
    Conference& conf = conferences[current_conference_id];
    for (auto& pair : conf.participants) {
        int uid = pair.first;
        auto& p = pair.second;
        
        // Simulate audio levels
        if (p.microphone_enabled) {
            // Random fluctuation if "speaking"
            if (p.is_speaking) {
                float target = 0.3f + (float)(rand() % 70) / 100.0f;
                p.sound_level += (target - p.sound_level) * 10.0f * delta_time;
            } else {
                 p.sound_level += (0.0f - p.sound_level) * 10.0f * delta_time;
            }
            
            // Randomly toggle speaking for fake users
            if (uid != current_user_id && uid >= 1000) {
                 if (rand() % 200 == 0) p.is_speaking = !p.is_speaking;
            }
        } else {
            p.sound_level = 0.0f;
            p.is_speaking = false;
        }
        
        // Remove old reactions (simple timeout logic imitation)
        // In real app we would track time per reaction
    }
}

FakeVideoStream ConferenceManager::GetFakeStream(int user_id) {
    if (fake_streams.find(user_id) != fake_streams.end()) {
        return fake_streams[user_id];
    }
    return FakeVideoStream{0.2f, 0.2f, 0.2f, "?", false, false};
}
