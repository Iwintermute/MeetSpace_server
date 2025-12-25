#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

#include "cNetIoContext.h"

namespace Sys {
    namespace Network {

        class cNetWebSocketServer {
        public:
            using tOnMessage = std::function<void(const std::string&, void*)>;
            using tOnBinary = std::function<void(const std::vector<uint8_t>&, void*)>;
            using tOnConnected = std::function<void(void*)>;
            using tOnDisconnected = std::function<void(void*)>;

            cNetWebSocketServer(Sys::Network::cNetIoContext::sIoStub& ctx, unsigned short port);
            ~cNetWebSocketServer();

            void fnSetOnMessage(tOnMessage fn) { m_fnOnMessage = std::move(fn); }
            void fnSetOnBinary(tOnBinary fn) { m_fnOnBinary = std::move(fn); }
            void fnSetOnConnected(tOnConnected fn) { m_fnOnConnected = std::move(fn); }
            void fnSetOnDisconnected(tOnDisconnected fn) { m_fnOnDisconnected = std::move(fn); }

            bool fnStart();
            void fnStop();

            void fnSendText(void* pSession, const std::string& txt);
            void fnSendBinary(void* pSession, const std::vector<uint8_t>& data);
            void fnBroadcastText(const std::string& txt, void* pSkip = nullptr);
            void fnBroadcastBinary(const std::vector<uint8_t>& data, void* pSkip = nullptr);

        private:
            void fnDoAccept();
            void fnOnSessionClosed(void* ptr);

            Sys::Network::cNetIoContext::sIoStub& m_ctx;
            unsigned short m_port;
            bool           m_running;

            tOnMessage       m_fnOnMessage;
            tOnBinary        m_fnOnBinary;
            tOnConnected     m_fnOnConnected;
            tOnDisconnected  m_fnOnDisconnected;

            std::mutex m_mtxSessions;
            std::unordered_map<void*, std::weak_ptr<void>> m_sessions;
        };

    } // namespace Network
} // namespace Sys
