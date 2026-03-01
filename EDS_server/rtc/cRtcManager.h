#pragma once
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <mutex>
#include <functional>
#include <nlohmann/json.hpp>

namespace Sys::Rtc {

    using json = nlohmann::json;

    class cRtcPeer;

    class cRtcManager {
    public:
        using tSendToClient = std::function<void(void* pSession, const std::string& sMsg)>;

        explicit cRtcManager(tSendToClient fnSend);
        ~cRtcManager();

        bool fnInit();
        void fnShutdown();

        std::shared_ptr<cRtcPeer> fnGetPeer(const std::string& peerKey);

        // signaling:
        // {type:"webrtc_offer", peer:"...", sdp:"..."}
        // {type:"webrtc_ice", peer:"...", sdpMid:"...", candidate:"..."}
        // {type:"webrtc_close", peer:"..."}
        void fnOnSignalingMessage(void* pSession, const json& jMsg);

        // cleanup when WS disconnects
        std::vector<std::string> fnOnWsDisconnected(void* pSession); // returns peerKeys removed

        // Data relay hook:
        using tOnPeerBinary = std::function<void(const std::string& peerKey, const std::vector<uint8_t>& data)>;
        void fnSetOnPeerBinary(tOnPeerBinary cb) { m_onPeerBinary = std::move(cb); }

    private:
        std::shared_ptr<cRtcPeer> fnGetOrCreatePeer_Locked(const std::string& peerKey, void* pSession);
        void fnDestroyPeer_Locked(const std::string& peerKey);

        void fnSendJsonToSession(void* pSession, const json& j);

    private:
        struct sPeerEntry {
            std::shared_ptr<cRtcPeer> pPeer;
            void* pSession{};
        };

        tSendToClient m_fnSendToClient;

        std::mutex m_mtx;
        bool m_inited{ false };

        std::unordered_map<std::string, sPeerEntry> m_peers;               // peerKey -> peer+session
        std::unordered_map<void*, std::unordered_set<std::string>> m_sess; // session -> set(peerKey)

        tOnPeerBinary m_onPeerBinary;
    };

} // namespace Sys::Rtc