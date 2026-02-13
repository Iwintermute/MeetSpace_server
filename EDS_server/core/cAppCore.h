#pragma once
#include "../network/cNetIoContext.h"
#include "../network/cNetHttpServer.h"
#include "../network/cNetWebSocketServer.h"
#include "../rtc/cRtcManager.h"
#include "../conference/cConferenceManager.h"
#include <memory>

namespace Sys {

    class cAppCore {
    public:
        cAppCore();
        ~cAppCore();

        bool fnInit(unsigned short wsPort, unsigned short httpPort);
        void fnRun();
        void fnShutdown();

    private:
        Network::cNetIoContext m_ioCtx;
        std::unique_ptr<Network::cNetWebSocketServer> m_wsServer;
        std::unique_ptr<Network::cNetHttpServer> m_httpServer;

        std::shared_ptr<Rtc::cRtcManager> m_rtcManager;
        Conference::cConferenceManager    m_confMgr;
        std::mutex m_peerSessMtx;
        std::unordered_map<std::string, void*> m_peerSess; // peerKey -> session

        void fnRememberPeerSession(const std::string& peerKey, void* session);
        void fnForgetPeerSession(const std::string& peerKey);
        void* fnGetSessionByPeer(const std::string& peerKey);

        void fnOnWsMessage(const std::string& msg, void* session);
        void fnOnWsConnected(void* session);
        void fnOnWsDisconnected(void* session);

        void fnHandleConferenceMsg(void* session, const nlohmann::json& j);
    };

} // namespace Sys
