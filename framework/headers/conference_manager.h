#pragma once
#include "conference_state.h"
#include <vector>
#include <string>
#include <map>
#include <unordered_map>

class ConferenceManager {
public:
    ConferenceManager();
    ~ConferenceManager();

    // Создание/управление
    int CreateConference(const ConferenceSettings& settings);
    bool JoinConference(int conference_id, const std::string& password = "");
    bool LeaveConference(int conference_id);
    bool JoinConferenceByCode(const std::string& invite_code);
    bool EndConference(int conference_id);
    
    // Управление участниками
    bool UpdateParticipantRole(int conference_id, int user_id, UserRole role);
    bool MuteParticipant(int conference_id, int user_id, bool mute);
    bool RemoveParticipant(int conference_id, int user_id);
    bool MoveToWaitingRoom(int conference_id, int user_id);
    
    // Медиа управление
    bool ToggleMicrophone(int conference_id);
    bool ToggleCamera(int conference_id);
    bool StartScreenShare(int conference_id);
    bool StopScreenShare(int conference_id);
    bool ToggleRecording(int conference_id);
    
    // Реакции и взаимодействия
    void SendReaction(int conference_id, const std::string& emoji);
    void RaiseHand(int conference_id, bool raise);
    void SendChatMessage(int conference_id, const std::string& text);
    
    // Получение данных
    Conference* GetConference(int conference_id);
    std::vector<Conference> GetUserConferences(int user_id);
    std::vector<ConferenceMessage> GetMessages(int conference_id);
    
    // Helper functionality for demo
    void UpdateFakeData(float delta_time); // Call every frame to animate fake users
    FakeVideoStream GetFakeStream(int user_id);

    // Current State
    int current_conference_id = -1;
    int current_user_id = 0; // Assume 0 is self for now, or set externally

private:
    std::map<int, Conference> conferences;
    std::map<std::string, int> invite_index;
    std::map<int, FakeVideoStream> fake_streams;
    std::unordered_map<std::string, int> peer_user_ids;
    int next_peer_user_id = 10000;
    int pending_conference_local_id = -1;
    bool network_callbacks_registered = false;
    int next_conference_id = 1;

    std::string GenerateInviteCode(int conference_id);
    void GenerateFakeParticipants(Conference& conference, int count);
    int ResolvePeerUserId(const std::string& peer_id);
    void RegisterNetworkCallbacks();
    void SyncConferenceIdFromServer(int server_id);
    void EnsureNetworkReady();
};

extern ConferenceManager* conference_manager;
