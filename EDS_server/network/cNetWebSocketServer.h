#pragma once

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

namespace Sys {
    namespace Network {

        class cNetWebSocketServer {
        public:
            using tOnMessage = std::function<void(const std::string&, void*)>;
            using tOnBinary = std::function<void(const std::vector<uint8_t>&, void*)>;
            using tOnConnected = std::function<void(void*)>;
            using tOnDisconnected = std::function<void(void*)>;

            cNetWebSocketServer(boost::asio::io_context& ctx, unsigned short port);
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
            struct sWsSession : public std::enable_shared_from_this<sWsSession> {
                using tcp = boost::asio::ip::tcp;
                using websocket = boost::beast::websocket::stream<tcp::socket>;

                sWsSession(tcp::socket&& sock, cNetWebSocketServer* owner);
                ~sWsSession();

                void fnStart();
                void fnDoRead();
                void fnSendText(const std::string& txt);
                void fnSendBinary(const std::vector<uint8_t>& data);

                websocket            m_ws;
                boost::beast::flat_buffer m_buffer;

                cNetWebSocketServer* m_owner;
                bool                 m_bOpen;
            };

            void fnDoAccept();
            void fnOnSessionClosed(void* ptr);

            boost::asio::io_context& m_ctx;
            unsigned short                             m_port;
            std::unique_ptr<boost::asio::ip::tcp::acceptor> m_acceptor;
            bool                                        m_running;

            tOnMessage       m_fnOnMessage;
            tOnBinary        m_fnOnBinary;
            tOnConnected     m_fnOnConnected;
            tOnDisconnected  m_fnOnDisconnected;

            std::mutex m_mtxSessions;
            std::unordered_map<void*, std::weak_ptr<sWsSession>> m_sessions;
        };

    } // namespace Network
} // namespace Sys
