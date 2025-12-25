#include "cNetHttpServer.h"
#include <iostream>

namespace Sys {
    namespace Network {

        cNetHttpServer::cNetHttpServer(Sys::Network::cNetIoContext::sIoStub& ioCtx, unsigned short port)
            : m_rIoCtx(ioCtx), m_uPort(port), m_bRunning(false)
        {
            (void)m_rIoCtx; // unused in the stub implementation
        }

        cNetHttpServer::~cNetHttpServer() { fnStop(); }

        bool cNetHttpServer::fnStart()
        {
            if (m_bRunning) return true;
            m_bRunning = true;
            std::cout << "[HTTP] stub server started on port " << m_uPort << " (no Boost required)" << std::endl;
            return true;
        }

        void cNetHttpServer::fnStop()
        {
            if (!m_bRunning) return;
            m_bRunning = false;
            std::cout << "[HTTP] stub server stopped" << std::endl;
        }

        void cNetHttpServer::fnDoAccept()
        {
            // Stub: no actual network operations are performed.
        }

    } // namespace Network
} // namespace Sys
