#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <optional>
#include <nlohmann/json.hpp>

namespace Sys::Conference {

    using json = nlohmann::json;

    struct sConference {
        int id{};
        std::string title;
        std::string invite;
        std::unordered_set<std::string> peers; // peerKey
    };

    class cConferenceManager {
    public:
        cConferenceManager() = default;

        // returns {confId, invite}
        std::pair<int, std::string> fnCreateConference(const std::string& title);

        // returns confId if invite ok
        std::optional<int> fnJoinByInvite(const std::string& invite, const std::string& peerKey);

        void fnLeave(const std::string& peerKey);

        // for relay: who is in the same conf
        std::unordered_set<std::string> fnGetPeersInSameConf(const std::string& peerKey);

    private:
        std::string fnGenInvite();

    private:
        std::mutex m_mtx;
        int m_nextId{ 1 };

        std::unordered_map<int, sConference> m_confs;           // id->conf
        std::unordered_map<std::string, int> m_inviteToConf;    // invite->id
        std::unordered_map<std::string, int> m_peerToConf;      // peerKey->id
    };

} // namespace Sys::Conference
