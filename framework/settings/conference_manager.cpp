#include "../headers/conference_manager.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include "../net/cConferenceNet.h"
ConferenceManager* conference_manager = new ConferenceManager();

ConferenceManager::ConferenceManager() {}
ConferenceManager::~ConferenceManager() {}
int ConferenceManager::CreateConference(const ConferenceSettings& settings)
{
    net_error.clear();

    // ≈сли есть сеть и она готова Ч запускаем async создание, а ID вернЄм "предсказуемый" локальный,
    // чтобы UI мог сразу открыть экран. –еальный confId придЄт позже через PollNetwork().
    if (m_net && m_net->isConnected() && !m_net->isBusy())
    {
        net_busy = true;
        m_op = NetOp::Creating;
        m_pendingSettings = settings;

        std::string peerKey = "user_" + std::to_string(current_user_id);
        m_net->createConferenceAsync(settings.title, peerKey);

        // UI-friendly: создаЄм локальную оболочку с временным id (локальный next_conference_id),
        // а когда сеть вернЄт реальный confId Ч EnsureLocalConferenceShell() создаст/обновит запись.
        int tempId = next_conference_id++;
        EnsureLocalConferenceShell(tempId, settings, true);
        current_conference_id = tempId;
        return tempId;
    }

    // »наче Ч локальна€ демо конференци€
    int id = CreateConferenceLocal(settings);
    // ѕо UX обычно сразу подключаем создател€
    JoinConferenceLocal(id, settings.password);
    return id;
}

bool ConferenceManager::JoinConference(int conference_id, const std::string& password)
{
    net_error.clear();

    // ѕока протокол сети у теб€ Ч join “ќЋ№ ќ по invite (JoinConferenceByInviteAsync),
    // а join по confId по сети не реализован. ѕоэтому sync JoinConference делает локальный join.
    return JoinConferenceLocal(conference_id, password);
}

bool ConferenceManager::CreateConferenceAsync(const ConferenceSettings& settings)
{
    if (!m_net) { net_error = "No net"; return false; }
    if (!m_net->isConnected()) { net_error = "WS not connected"; return false; }
    if (m_net->isBusy()) { net_error = "Net busy"; return false; }

    net_error.clear();
    net_busy = true;
    m_op = NetOp::Creating;
    m_pendingSettings = settings;

    std::string peerKey = "user_" + std::to_string(current_user_id);
    m_net->createConferenceAsync(settings.title, peerKey);
    return true;
}

bool ConferenceManager::JoinConferenceByInviteAsync(const std::string& invite)
{
    if (!m_net) { net_error = "No net"; return false; }
    if (!m_net->isConnected()) { net_error = "WS not connected"; return false; }
    if (m_net->isBusy()) { net_error = "Net busy"; return false; }
    if (invite.empty()) { net_error = "Invite empty"; return false; }

    net_error.clear();
    net_busy = true;
    m_op = NetOp::Joining;
    m_pendingInvite = invite;

    std::string peerKey = "user_" + std::to_string(current_user_id);
    m_net->joinByInviteAsync(invite, peerKey);
    return true;
}

void ConferenceManager::PollNetwork()
{
    if (!m_net) return;

    net_busy = m_net->isBusy();
    if (!net_busy) {
        // если async уже завершилс€ Ч добьЄм результат
        if (m_op == NetOp::Creating) {
            std::string invite;
            if (m_net->pollCreateResult(invite)) {
                if (!invite.empty()) {
                    int confId = m_net->lastConfId();
                    last_created_invite = invite;
                    last_invite_code = invite;
                    current_conference_id = confId;

                    EnsureLocalConferenceShell(confId, m_pendingSettings, true);

                    // sync mic
                    auto* conf = GetConference(confId);
                    if (conf) {
                        bool mic = !conf->settings.auto_mute_on_join;
                        conf->participants[current_user_id].microphone_enabled = mic;
                        m_net->setMicEnabled(mic);
                    }
                }
                else {
                    net_error = m_net->lastError();
                }
                m_op = NetOp::None;
            }
        }
        else if (m_op == NetOp::Joining) {
            bool ok = false;
            if (m_net->pollJoinResult(ok)) {
                if (ok) {
                    int confId = m_net->lastConfId();
                    current_conference_id = confId;
                    last_invite_code = m_pendingInvite;

                    ConferenceSettings dummy{};
                    dummy.title = "Conference " + m_pendingInvite;
                    EnsureLocalConferenceShell(confId, dummy, false);

                    m_net->setMicEnabled(false);
                }
                else {
                    net_error = m_net->lastError();
                }
                m_op = NetOp::None;
            }
        }
    }
}

void ConferenceManager::EnsureLocalConferenceShell(int confId, const ConferenceSettings& settings, bool isHost)
{
    if (conferences.find(confId) == conferences.end()) {
        Conference conf;
        conf.id = confId;
        conf.settings = settings;
        if (conf.settings.title.empty())
            conf.settings.title = "Conference";
        conf.creator_id = isHost ? current_user_id : -1;
        conf.created_at = time(nullptr);
        conf.status = ConferenceStatus::Active;
        conferences[conf.id] = conf;
    }

    auto& conf = conferences[confId];
    ConferenceParticipant me;
    me.user_id = current_user_id;
    me.role = isHost ? UserRole::Host : UserRole::Participant;
    me.joined_at = time(nullptr);
    me.microphone_enabled = !settings.auto_mute_on_join;
    me.camera_enabled = !settings.auto_camera_on_join;
    conf.participants[current_user_id] = me;
}

int ConferenceManager::CreateConferenceLocal(const ConferenceSettings& settings)
{
    Conference conf;
    conf.id = next_conference_id++;
    conf.settings = settings;
    conf.creator_id = current_user_id;
    conf.created_at = time(nullptr);
    conf.status = ConferenceStatus::Scheduled;

    ConferenceParticipant creator;
    creator.user_id = current_user_id;
    creator.role = UserRole::Host;
    creator.joined_at = time(nullptr);
    creator.microphone_enabled = !settings.auto_mute_on_join;
    creator.camera_enabled = !settings.auto_camera_on_join;

    conf.participants[current_user_id] = creator;
    conferences[conf.id] = conf;
    return conf.id;
}

bool ConferenceManager::JoinConferenceLocal(int conference_id, const std::string& password)
{
    if (conferences.find(conference_id) == conferences.end()) return false;
    Conference& conf = conferences[conference_id];

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
    current_conference_id = conference_id;
    conf.status = ConferenceStatus::Active;
    return true;
}

bool ConferenceManager::LeaveConference(int conference_id)
{
    if (conference_id == current_conference_id) {
        current_conference_id = -1;
        if (m_net && m_net->isInConference())
            m_net->leave();
    }

    auto it = conferences.find(conference_id);
    if (it != conferences.end())
        it->second.participants.erase(current_user_id);

    return true;
}

bool ConferenceManager::EndConference(int conference_id)
{
    auto it = conferences.find(conference_id);
    if (it == conferences.end()) return false;

    it->second.status = ConferenceStatus::Ended;
    if (current_conference_id == conference_id) {
        current_conference_id = -1;
        if (m_net && m_net->isInConference())
            m_net->leave();
    }
    return true;
}

bool ConferenceManager::ToggleMicrophone(int conference_id)
{
    auto it = conferences.find(conference_id);
    if (it == conferences.end()) return false;

    auto& conf = it->second;
    auto pit = conf.participants.find(current_user_id);
    if (pit == conf.participants.end()) return false;

    pit->second.microphone_enabled = !pit->second.microphone_enabled;

    if (m_net && m_net->isInConference())
        m_net->setMicEnabled(pit->second.microphone_enabled);

    return pit->second.microphone_enabled;
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
        p.is_speaking = (i == 0);
        p.hand_raised = (i == 3);
        p.joined_at = time(nullptr) - rand() % 3600;

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

Conference* ConferenceManager::GetConference(int conference_id)
{
    auto it = conferences.find(conference_id);
    if (it == conferences.end()) return nullptr;
    return &it->second;
}

std::vector<Conference> ConferenceManager::GetUserConferences(int user_id) {
    std::vector<Conference> result;
    for (const auto& pair : conferences) {
        result.push_back(pair.second);
    }
    return result;
}

std::vector<ConferenceMessage> ConferenceManager::GetMessages(int conference_id)
{
    auto it = conferences.find(conference_id);
    if (it == conferences.end()) return {};
    return it->second.messages;
}

void ConferenceManager::SendReaction(int conference_id, const std::string& emoji) {
    if (conferences.find(conference_id) != conferences.end()) {
        auto& p = conferences[conference_id].participants[current_user_id];
        p.active_reactions.push_back(emoji);
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

        if (p.microphone_enabled) {
            if (p.is_speaking) {
                float target = 0.3f + (float)(rand() % 70) / 100.0f;
                p.sound_level += (target - p.sound_level) * 10.0f * delta_time;
            }
            else {
                p.sound_level += (0.0f - p.sound_level) * 10.0f * delta_time;
            }

            if (uid != current_user_id && uid >= 1000) {
                if (rand() % 200 == 0) p.is_speaking = !p.is_speaking;
            }
        }
        else {
            p.sound_level = 0.0f;
            p.is_speaking = false;
        }
    }
}

FakeVideoStream ConferenceManager::GetFakeStream(int user_id) {
    if (fake_streams.find(user_id) != fake_streams.end()) {
        return fake_streams[user_id];
    }
    return FakeVideoStream{ 0.2f, 0.2f, 0.2f, "?", false, false };
}

