#include "cAppCore.h"
#include "../util/cLogger.h"
#include <iostream>

namespace Sys {

    cAppCore::cAppCore() {}
    cAppCore::~cAppCore() { fnShutdown(); }

    bool cAppCore::fnInit(unsigned short wsPort, unsigned short httpPort) {
        m_ioCtx.fnInit();

        m_rtcManager = std::make_shared<Rtc::cRtcManager>(
            [this](void* session, const std::string& msg) {
                if (m_wsServer) m_wsServer->fnSetOnMessage([msg](const std::string&, void*) {});
            });

        m_wsServer = std::make_unique<Network::cNetWebSocketServer>(m_ioCtx.fnIo(), wsPort);
        m_wsServer->fnSetOnMessage([this](const std::string& msg, void* session) { fnOnWsMessage(msg, session); });
        m_wsServer->fnSetOnConnected([this](void* s) { fnOnWsConnected(s); });
        m_wsServer->fnSetOnDisconnected([this](void* s) { fnOnWsDisconnected(s); });

        m_httpServer = std::make_unique<Network::cNetHttpServer>(m_ioCtx.fnIo(), httpPort);
        m_httpServer->fnSetHealthFn([]() { return R"({"status":"ok"})"; });
        m_httpServer->fnSetMetricsFn([]() { return R"({"peers":20})"; });

        m_mediaPipeline = std::make_unique<Media::cMediaPipeline>(m_rtcManager);

        return m_wsServer->fnStart() && m_httpServer->fnStart() && m_ioCtx.fnStart();
    }

    void cAppCore::fnRun() {
        while (true) std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    void cAppCore::fnShutdown() {
        if (m_wsServer) m_wsServer->fnStop();
        if (m_httpServer) m_httpServer->fnStop();
        m_ioCtx.fnStop();
    }

    void cAppCore::fnOnWsMessage(const std::string& msg, void* session) {
        try {
            nlohmann::json j = nlohmann::json::parse(msg);
            m_rtcManager->fnOnSignalingMessage(session, j);
        }
        catch (...) {}
    }

    void cAppCore::fnOnWsConnected(void* session) {
        Sys::cLogger::fnLog(Sys::cLogger::Level::Info, "WS Connected");
    }

    void cAppCore::fnOnWsDisconnected(void* session) {
        Sys::cLogger::fnLog(Sys::cLogger::Level::Info, "WS Disconnected");
    }

} // namespace Sys
