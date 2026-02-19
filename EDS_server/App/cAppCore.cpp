#include "cAppCore.h"
#include "utils/cLogger.h"
#include <iostream>
#include <thread>
#include "rtc/cRtcPeer.h"

#include "managers/ModuleRegistry.h"

namespace Sys {

    cAppCore::cAppCore() {}
    cAppCore::~cAppCore() { fnShutdown(); }

    bool fnInitNew(unsigned short wsPort, unsigned short httpPort) {
        Network::cNetIoContext m_ioCtx;


        ModuleRegistry Registry;

        m_ioCtx.fnInit();
        auto WSServer = Registry.registerModule<Network::cNetWebSocketServer>(m_ioCtx.fnIo(), wsPort);
        auto HTTPServer = Registry.registerModule<Network::cNetHttpServer>(m_ioCtx.fnIo(), httpPort);
        //std::string name = "name";

        //BaseModule* baseModule = Registry.registerModule<BaseModule>(name);


        Registry.initializeAll();

    }

    bool cAppCore::fnInit(unsigned short wsPort, unsigned short httpPort)
    {
        m_ioCtx.fnInit();

        m_wsServer = std::make_unique<Network::cNetWebSocketServer>(m_ioCtx.fnIo(), wsPort);
        m_httpServer = std::make_unique<Network::cNetHttpServer>(m_ioCtx.fnIo(), httpPort);

        // RTC manager -> send signaling via WS
        m_rtcManager = std::make_shared<Rtc::cRtcManager>(
            [this](void* session, const std::string& msg)
            {
                if (m_wsServer) m_wsServer->fnSendText(session, msg);
            }
        );
        m_rtcManager->fnInit();

        // RTC binary relay -> conference peers
        m_rtcManager->fnSetOnPeerBinary([this](const std::string& fromPeer, const std::vector<uint8_t>& data)
            {
                auto peers = m_confMgr.fnGetPeersInSameConf(fromPeer);
                for (const auto& peer : peers) {
                    if (peer == fromPeer) continue;
                    auto p = m_rtcManager->fnGetPeer(peer);
                    if (p) p->fnSendBinary(data);
                }
            });

        // WS callbacks
        m_wsServer->fnSetOnMessage([this](const std::string& msg, void* session) { fnOnWsMessage(msg, session); });
        m_wsServer->fnSetOnConnected([this](void* s) { fnOnWsConnected(s); });
        m_wsServer->fnSetOnDisconnected([this](void* s) { fnOnWsDisconnected(s); });

        // HTTP
        m_httpServer->fnSetHealthFn([]() { return R"({"status":"ok"})"; });
        m_httpServer->fnSetMetricsFn([]() { return R"({"metrics":{"ok":1}})"; });

        return m_wsServer->fnStart() && m_httpServer->fnStart() && m_ioCtx.fnStart();
    }

    void cAppCore::fnRun()
    {
        // тут можно сделать join потоков io_context, но у тебя loop — оставляю
        while (true) std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    void cAppCore::fnShutdown()
    {
        if (m_wsServer)   m_wsServer->fnStop();
        if (m_httpServer) m_httpServer->fnStop();
        if (m_rtcManager) m_rtcManager->fnShutdown();
        m_ioCtx.fnStop();
    }

    void cAppCore::fnOnWsMessage(const std::string& msg, void* session)
    {
        try {
            auto j = nlohmann::json::parse(msg);
            std::string type = j.value("type", "");

            // === Получаем доверенный peerKey от сессии ===
            std::string peerKey;
            {
                std::lock_guard<std::mutex> lg(m_peerSessMtx);
                auto it = m_sessionToPeer.find(session);
                if (it != m_sessionToPeer.end()) peerKey = it->second;
            }

            if (peerKey.empty()) {
                // Первый пакет до peer_assigned — игнорируем
                return;
            }

            // Опционально: проверяем, что клиент не врёт
            std::string clientPeer = j.value("peer", "");
            if (!clientPeer.empty() && clientPeer != peerKey) {
                Sys::cLogger::fnLog(Sys::cLogger::Level::Warning,
                    "Peer impersonation attempt! Expected: " + peerKey + ", got: " + clientPeer);
                return;
            }

            // Conference messages
            if (type == "conf_create" || type == "conf_join" ||
                type == "conf_leave" || type == "conf_mic") {
                fnHandleConferenceMsg(session, j, peerKey);   // ← передаём trusted peerKey
                return;
            }

            // Signaling (webrtc_*)
            if (type.rfind("webrtc_", 0) == 0) {
                nlohmann::json fixed = j;
                fixed["peer"] = peerKey;                     // принудительно
                if (m_rtcManager) m_rtcManager->fnOnSignalingMessage(session, fixed);
            }

        }
        catch (...) {
            Sys::cLogger::fnLog(Sys::cLogger::Level::Error, "Error in fnOnWsMessage!");
        }
    }

    void cAppCore::fnRememberPeerSession(const std::string& peerKey, void* session) {
        std::lock_guard<std::mutex> lg(m_peerSessMtx);
        m_peerSess[peerKey] = session;
        m_sessionToPeer[session] = peerKey;
    }

    void cAppCore::fnForgetPeerSession(const std::string& peerKey) {
        std::lock_guard<std::mutex> lg(m_peerSessMtx);
        auto it = m_peerSess.find(peerKey);
        if (it != m_peerSess.end()) {
            m_sessionToPeer.erase(it->second);
            m_peerSess.erase(it);
        }
    }
    void* cAppCore::fnGetSessionByPeer(const std::string& peerKey) {
        std::lock_guard<std::mutex> lg(m_peerSessMtx);
        auto it = m_peerSess.find(peerKey);
        return it == m_peerSess.end() ? nullptr : it->second;
    }
    void cAppCore::fnHandleConferenceMsg(void* session, const nlohmann::json& j, const std::string& peerKey)
    {
        const std::string type = j.value("type", "");

        if (type == "conf_create") {
            std::string title = j.value("title", "Conference");

            auto [confId, invite] = m_confMgr.fnCreateConference(title);
            m_confMgr.fnJoinByInvite(invite, peerKey);        // trusted

            nlohmann::json resp{
                {"type", "conf_created"},
                {"confId", confId},
                {"invite", invite},
                {"peer", peerKey}
            };
            m_wsServer->fnSendText(session, resp.dump());
            return;
        }

        if (type == "conf_join") {
            const std::string invite = j.value("invite", "");
            const std::string peer = peerKey;

            auto confId = m_confMgr.fnJoinByInvite(invite, peer);

            if (!confId) {
                nlohmann::json resp = {
                    {"type","conf_joined"},
                    {"ok",false},
                    {"reason","bad_invite"},
                    {"peer",peer}
                };
                if (m_wsServer) m_wsServer->fnSendText(session, resp.dump());
                return;
            }

            // peers list (включая joiner)
            auto peers = m_confMgr.fnGetPeersInSameConf(peer);

            // ACK joiner
            nlohmann::json resp = {
                {"type","conf_joined"},
                {"ok",true},
                {"confId",*confId},
                {"peer",peer},
                {"peers", peers}
            };
            if (m_wsServer) m_wsServer->fnSendText(session, resp.dump());

            // notify others
            nlohmann::json ev = {
                {"type","conf_peer_joined"},
                {"confId",*confId},
                {"peer",peer}
            };
            for (const auto& p : peers) {
                if (p == peer) continue;
                if (auto s = fnGetSessionByPeer(p)) m_wsServer->fnSendText(s, ev.dump());
            }
            return;
        }

        if (type == "conf_leave") {
            const std::string peer = j.value("peer", "");
            if (peer.empty()) return;

            auto peersBefore = m_confMgr.fnGetPeersInSameConf(peer);

            m_confMgr.fnLeave(peer);
            fnForgetPeerSession(peer);

            nlohmann::json ev = { {"type","conf_peer_left"}, {"peer",peer} };
            for (const auto& p : peersBefore) {
                if (p == peer) continue;
                if (auto s = fnGetSessionByPeer(p)) m_wsServer->fnSendText(s, ev.dump());
            }
            return;
        }

        if (type == "conf_mic") {
            const std::string peer = j.value("peer", "");
            const bool enabled = j.value("enabled", true);
            if (peer.empty()) return;

            auto peers = m_confMgr.fnGetPeersInSameConf(peer);

            nlohmann::json ev = { {"type","conf_peer_mic"}, {"peer",peer}, {"enabled",enabled} };
            for (const auto& p : peers) {
                if (p == peer) continue;
                if (auto s = fnGetSessionByPeer(p)) m_wsServer->fnSendText(s, ev.dump());
            }
            return;
        }
    }

    void cAppCore::fnOnWsConnected(void* session)
    {
        std::string peerKey = fnGeneratePeerKey();

        fnRememberPeerSession(peerKey, session);

        nlohmann::json assign{
            {"type", "peer_assigned"},
            {"peer", peerKey}
        };

        if (m_wsServer) m_wsServer->fnSendText(session, assign.dump());

        Sys::cLogger::fnLog(Sys::cLogger::Level::Info,
            "Peer assigned: " + peerKey);
    }

    void cAppCore::fnOnWsDisconnected(void* session)
    {
        Sys::cLogger::fnLog(Sys::cLogger::Level::Info, "WS Disconnected");

       
        Sys::cLogger::fnLog(Sys::cLogger::Level::Info, "WS Disconnected");

        if (!m_rtcManager) return;

        auto peerKeys = m_rtcManager->fnOnWsDisconnected(session);
        for (const auto& pk : peerKeys)
        {
            auto peersBefore = m_confMgr.fnGetPeersInSameConf(pk);

            // убираем из конфы
            m_confMgr.fnLeave(pk);

            // убираем связь peer->session
            fnForgetPeerSession(pk);

            // notify others
            nlohmann::json ev = { {"type","conf_peer_left"}, {"peer",pk} };
            for (const auto& p : peersBefore) {
                if (p == pk) continue;
                if (auto s = fnGetSessionByPeer(p)) m_wsServer->fnSendText(s, ev.dump());
            }
        }
    }
    std::string cAppCore::fnGeneratePeerKey()
    {
        static const char* hex = "0123456789abcdef";
        std::random_device rd;
        std::mt19937_64 rng(rd());                     // 64-битный генератор
        std::uniform_int_distribution<int> dist(0, 15);

        std::string key;
        key.reserve(32);
        for (int i = 0; i < 32; ++i) {
            key.push_back(hex[dist(rng)]);
        }
        return key;
    }

} // namespace Sys
