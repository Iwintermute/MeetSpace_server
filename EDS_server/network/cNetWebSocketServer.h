#pragma once

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <functional>
#include <memory>
#include <string>

namespace Sys {
    namespace Network {

        class cNetWebSocketServer {
        public:
            using tOnMessage = std::function<void(const std::string&, void*)>;
            using tOnConnected = std::function<void(void*)>;
            using tOnDisconnected = std::function<void(void*)>;

            cNetWebSocketServer(boost::asio::io_context& ctx, unsigned short port);
            ~cNetWebSocketServer();

            void fnSetOnMessage(tOnMessage fn) { m_fnOnMessage = std::move(fn); }
            void fnSetOnConnected(tOnConnected fn) { m_fnOnConnected = std::move(fn); }
            void fnSetOnDisconnected(tOnDisconnected fn) { m_fnOnDisconnected = std::move(fn); }

            bool fnStart();
            void fnStop();

        private:
            struct sWsSession : public std::enable_shared_from_this<sWsSession> {
                using tcp = boost::asio::ip::tcp;
                using websocket = boost::beast::websocket::stream<tcp::socket>;

                sWsSession(tcp::socket&& sock, cNetWebSocketServer* owner);
                ~sWsSession();

                void fnStart();
                void fnDoRead();
                void fnSendText(const std::string& txt);

                websocket            m_ws;
                boost::beast::flat_buffer m_buffer;

                cNetWebSocketServer* m_owner;
                bool                 m_bOpen;
            };

            void fnDoAccept();

            boost::asio::io_context& m_ctx;
            unsigned short                             m_port;
            std::unique_ptr<boost::asio::ip::tcp::acceptor> m_acceptor;
            bool                                        m_running;

            tOnMessage       m_fnOnMessage;
            tOnConnected     m_fnOnConnected;
            tOnDisconnected  m_fnOnDisconnected;
        };

    } // namespace Network
} // namespace Sys
