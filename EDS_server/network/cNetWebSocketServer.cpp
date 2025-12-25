#include "cNetWebSocketServer.h"
#include <iostream>

namespace Sys {
    namespace Network {

        cNetWebSocketServer::cNetWebSocketServer(Sys::Network::cNetIoContext::sIoStub& ctx, unsigned short port)
            : m_ctx(ctx)
            , m_port(port)
            , m_running(false)
        {
            (void)m_ctx; // unused in the stub implementation
        }

        cNetWebSocketServer::~cNetWebSocketServer()
        {
            fnStop();
        }

        bool cNetWebSocketServer::fnStart()
        {
            if (m_running) return true;
            m_running = true;
            std::cout << "[WS] stub websocket server started on port " << m_port << std::endl;
            return true;
        }

        void cNetWebSocketServer::fnStop()
        {
            if (!m_running) return;
            m_running = false;
            std::cout << "[WS] stub websocket server stopped" << std::endl;
            std::lock_guard<std::mutex> lg(m_mtxSessions);
            m_sessions.clear();
        }

        void cNetWebSocketServer::fnSendText(void* pSession, const std::string& txt)
        {
            (void)pSession;
            (void)txt;
            // Stub: no network transport, but keeping interface for callers.
        }

        void cNetWebSocketServer::fnSendBinary(void* pSession, const std::vector<uint8_t>& data)
        {
            (void)pSession;
            (void)data;
        }

        void cNetWebSocketServer::fnBroadcastText(const std::string& txt, void* pSkip)
        {
            (void)txt;
            (void)pSkip;
        }

        void cNetWebSocketServer::fnBroadcastBinary(const std::vector<uint8_t>& data, void* pSkip)
        {
            (void)data;
            (void)pSkip;
        }

        void cNetWebSocketServer::fnDoAccept()
        {
            // Stub: no accept loop without Boost.
        }

        void cNetWebSocketServer::fnOnSessionClosed(void* ptr)
        {
            std::lock_guard<std::mutex> lg(m_mtxSessions);
            m_sessions.erase(ptr);
        }

    } // namespace Network
} // namespace Sys
