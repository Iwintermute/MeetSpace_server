#pragma once

#include <boost/asio.hpp>
#include <boost/beast.hpp>

#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace eds::server_new::mediasoup::transport {

class WebSocketServer {
public:
    using OnMessage = std::function<void(const std::string&, void*)>;
    using OnConnected = std::function<void(void*)>;
    using OnDisconnected = std::function<void(void*)>;

    WebSocketServer(boost::asio::io_context& context, unsigned short port);
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

private:
    struct Session : std::enable_shared_from_this<Session> {
        using tcp = boost::asio::ip::tcp;
        using ws_stream = boost::beast::websocket::stream<tcp::socket>;

        Session(tcp::socket&& socket, WebSocketServer* owner);

        void start();
        void doRead();
        void enqueueText(std::string text);
        void doWrite();
        void close();

        ws_stream websocket;
        boost::beast::flat_buffer buffer;
        WebSocketServer* owner = nullptr;
        std::deque<std::string> outQueue;
        bool open = false;
        bool writing = false;
    };

    void doAccept();
    void registerSession(void* key, std::shared_ptr<Session> session);
    void unregisterSession(void* key);

private:
    boost::asio::io_context& context_;
    unsigned short port_ = 0;
    std::unique_ptr<boost::asio::ip::tcp::acceptor> acceptor_;
    bool running_ = false;

    OnMessage onMessage_;
    OnConnected onConnected_;
    OnDisconnected onDisconnected_;

    std::mutex sessionsMutex_;
    std::unordered_map<void*, std::weak_ptr<Session>> sessions_;
};

} // namespace eds::server_new::mediasoup::transport
