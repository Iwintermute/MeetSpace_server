#pragma once
#include "../network/cNetIoContext.h"
#include "../network/cNetHttpServer.h"
#include "../network/cNetWebSocketServer.h"
#include "../rtc/cRtcManager.h"
#include "../Media/cMediaPipeline.h"
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
        std::unique_ptr<Media::cMediaPipeline> m_mediaPipeline;

        void fnOnWsMessage(const std::string& msg, void* session);
        void fnOnWsConnected(void* session);
        void fnOnWsDisconnected(void* session);
    };

} // namespace Sys
