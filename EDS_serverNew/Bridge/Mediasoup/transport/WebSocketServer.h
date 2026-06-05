#pragma once

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>

#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace eds::server_new::mediasoup::transport {
    struct WebSocketTlsOptions {
        bool enabled = false;
        std::string certificateChainFile;
        std::string privateKeyFile;
        std::string caCertificateFile;
        std::string dhParamsFile;
        bool requireClientCertificate = false;
    };

    class WebSocketServer {
    public:
        using OnMessage = std::function<void(const std::string&, void*)>;
        using OnConnected = std::function<void(void*)>;
        using OnDisconnected = std::function<void(void*)>;
        WebSocketServer(
            boost::asio::io_context& context,
            unsigned short port,
            WebSocketTlsOptions tlsOptions = {});
        ~WebSocketServer();

        void setOnMessage(OnMessage callback) {
            onMessage_ = std::move(callback);
        }

        void setOnConnected(OnConnected callback) {
            onConnected_ = std::move(callback);
        }

        void setOnDisconnected(OnDisconnected callback) {
            onDisconnected_ = std::move(callback);
        }

        bool start();
        void stop();
        bool sendText(void* session, const std::string& text);
        std::size_t sendTexts(const std::vector<void*>& sessions, const std::string& text);
        bool closeSession(void* session);

    private:
        struct SessionHandle : std::enable_shared_from_this<SessionHandle> {
            virtual ~SessionHandle() = default;
            virtual void start() = 0;
            virtual void enqueueText(std::string text) = 0;
            virtual void close() = 0;
            virtual void detachOwner() = 0;
        };

        struct Session final : SessionHandle {
            using tcp = boost::asio::ip::tcp;
            using ws_stream = boost::beast::websocket::stream<tcp::socket>;

            Session(tcp::socket&& socket, WebSocketServer* ownerIn);
            void start() override;
            void enqueueText(std::string text) override;
            void close() override;
            void detachOwner() override;

            void doAccept();
            void doRead();
            void doWrite();

            ws_stream websocket;
            boost::beast::flat_buffer buffer;
            WebSocketServer* owner = nullptr;
            std::deque<std::string> outQueue;
            bool open = false;
            bool writing = false;
            bool closing = false;
        };

        struct TlsSession final : SessionHandle {
            using tcp = boost::asio::ip::tcp;
            using ssl_context = boost::asio::ssl::context;
            using ws_stream = boost::beast::websocket::stream<boost::beast::ssl_stream<tcp::socket>>;

            TlsSession(
                tcp::socket&& socket,
                WebSocketServer* ownerIn,
                std::shared_ptr<ssl_context> tlsContextIn);

            void start() override;
            void enqueueText(std::string text) override;
            void close() override;
            void detachOwner() override;

            void doTlsHandshake();
            void doAccept();
            void doRead();
            void doWrite();

            ws_stream websocket;
            boost::beast::flat_buffer buffer;
            std::shared_ptr<ssl_context> tlsContext;
            WebSocketServer* owner = nullptr;
            std::deque<std::string> outQueue;
            bool open = false;
            bool writing = false;
            bool closing = false;
        };

        void doAccept();
        void registerSession(void* key, std::shared_ptr<SessionHandle> session);
        void unregisterSession(void* key);
        bool configureTlsContext();

    private:
        boost::asio::io_context& context_;
        unsigned short port_ = 0;
        WebSocketTlsOptions tlsOptions_;
        std::unique_ptr<boost::asio::ip::tcp::acceptor> acceptor_;
        std::shared_ptr<boost::asio::ssl::context> tlsContext_;
        bool running_ = false;

        OnMessage onMessage_;
        OnConnected onConnected_;
        OnDisconnected onDisconnected_;

        std::mutex sessionsMutex_;
        std::unordered_map<void*, std::weak_ptr<SessionHandle>> sessions_;
    };

} // namespace eds::server_new::mediasoup::transport