#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <functional>
#include "../../networkEDS/include/nlohmann/json.hpp"

namespace Sys {
    namespace Rtc {
        class cRtcPeer;
    }
}

namespace Sys {
    namespace Rtc {

        using json = nlohmann::json;

        class cRtcManager {
        public:
            using tSendToClient = std::function<void(void* pSession, const std::string& sMsg)>;

            cRtcManager(tSendToClient fnSend);
            ~cRtcManager();

            bool fnInit();
            void fnShutdown();

            std::shared_ptr<cRtcPeer> fnCreatePeer(const std::string& sPeerId, void* pSession);
            void fnDestroyPeer(const std::string& sPeerId);

            void fnOnSignalingMessage(void* pSession, const json& jMsg);

            void fnBroadcast(const std::string& sFromPeer, const std::string& sMsg);

        private:
            void fnSendJsonToSession(void* pSession, const json& j);

        private:
            struct sPeerEntry {
                std::shared_ptr<cRtcPeer> pPeer;
                void* pSession;
            };
            tSendToClient m_fnSendToClient;
            std::unordered_map<std::string, sPeerEntry> m_mPeers;
            std::mutex m_mtx;
            bool m_bInited;
        };

    } // namespace Rtc
} // namespace Sys
