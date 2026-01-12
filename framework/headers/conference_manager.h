#pragma once
#include "conference_state.h"
#include <vector>
#include <string>
#include <map>
#include "../net/cConferenceNet.h"

class ConferenceManager {
public:
    ConferenceManager();
    ~ConferenceManager();

    void SetNet(cConferenceNet* net) { m_net = net; }

    // ---- SYNC API (то, что зовёт conference_widgets) ----
    // Создать конференцию (локально или по сети) и вернуть confId (или -1 при ошибке)
   int  CreateConference(const ConferenceSettings& settings);

    // Войти в конференцию по confId (локально) или по invite, если он есть
    bool JoinConference(int conference_id, const std::string& password = "");

    // ---- ASYNC API (если нужно из UI-кадра) ----
    bool CreateConferenceAsync(const ConferenceSettings& settings);
    bool JoinConferenceByInviteAsync(const std::string& invite);
    void PollNetwork();

    // локальные (демо)
    int  CreateConferenceLocal(const ConferenceSettings& settings);
    bool JoinConferenceLocal(int conference_id, const std::string& password = "");
    bool LeaveConference(int conference_id);
    bool EndConference(int conference_id);

    // Медиа
    bool ToggleMicrophone(int conference_id);
    bool ToggleCamera(int conference_id);
    bool ToggleRecording(int conference_id);

    // Реакции/чат
    void SendReaction(int conference_id, const std::string& emoji);
    void RaiseHand(int conference_id, bool raise);
    void SendChatMessage(int conference_id, const std::string& text);

    Conference* GetConference(int conference_id);
    std::vector<Conference> GetUserConferences(int user_id);
    std::vector<ConferenceMessage> GetMessages(int conference_id);

    void UpdateFakeData(float delta_time);
    FakeVideoStream GetFakeStream(int user_id);

    // UI состояния
    int current_conference_id = -1;
    int current_user_id = 0;

    std::string last_invite_code;
    std::string last_created_invite;

    bool net_busy = false;
    std::string net_error;

private:
    std::map<int, Conference> conferences;
    std::map<int, FakeVideoStream> fake_streams;
    int next_conference_id = 1;

    cConferenceNet* m_net{ nullptr };

    enum class NetOp { None, Creating, Joining };
    NetOp m_op{ NetOp::None };

    ConferenceSettings m_pendingSettings{};
    std::string m_pendingInvite{};

    void GenerateFakeParticipants(Conference& conference, int count);
    void EnsureLocalConferenceShell(int confId, const ConferenceSettings& settings, bool isHost);
};

extern ConferenceManager* conference_manager;
