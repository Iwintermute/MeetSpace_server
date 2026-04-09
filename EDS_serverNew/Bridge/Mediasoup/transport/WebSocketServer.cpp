#include "Bridge/Mediasoup/transport/WebSocketServer.h"

#include <iostream>

namespace eds::server_new::mediasoup::transport {

    namespace net = boost::asio;
    namespace beast = boost::beast;
    namespace websocket = beast::websocket;
    using tcp = boost::asio::ip::tcp;

    WebSocketServer::Session::Session(tcp::socket&& socket, WebSocketServer* ownerIn)
        : websocket(std::move(socket)),
        owner(ownerIn) {
    }

    void WebSocketServer::Session::start() {
        auto self = shared_from_this();

        net::dispatch(websocket.get_executor(), [self]() {
            self->doAccept();
            });
    }

    void WebSocketServer::Session::doAccept() {
        auto self = shared_from_this();

        websocket.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));

        websocket.async_accept(
            net::bind_executor(
                websocket.get_executor(),
                [self](beast::error_code errorCode) {
                    if (errorCode) {
                        std::cerr << "[MediasoupWS] accept failed: " << errorCode.message() << '\n';
                        self->close();
                        return;
                    }

                    self->open = true;

                    if (self->owner) {
                        self->owner->registerSession(self.get(), self);

                        if (self->owner->onConnected_) {
                            self->owner->onConnected_(self.get());
                        }
                    }

                    self->doRead();
                }));
    }

    void WebSocketServer::Session::doRead() {
        if (!open || closing) {
            return;
        }

        auto self = shared_from_this();

        websocket.async_read(
            buffer,
            net::bind_executor(
                websocket.get_executor(),
                [self](beast::error_code errorCode, std::size_t bytesRead) {
                    if (errorCode) {
                        self->close();
                        return;
                    }

                    std::string message = beast::buffers_to_string(self->buffer.data());
                    self->buffer.consume(bytesRead);

                    if (self->owner && self->owner->onMessage_) {
                        self->owner->onMessage_(message, self.get());
                    }

                    self->doRead();
                }));
    }

    void WebSocketServer::Session::enqueueText(std::string text) {
        auto self = shared_from_this();

        net::post(
            websocket.get_executor(),
            [self, text = std::move(text)]() mutable {
                if (!self->open || self->closing) {
                    return;
                }

                self->outQueue.push_back(std::move(text));

                if (self->writing) {
                    return;
                }

                self->writing = true;
                self->doWrite();
            });
    }

    void WebSocketServer::Session::doWrite() {
        if (!open || closing || outQueue.empty()) {
            writing = false;
            return;
        }

        auto self = shared_from_this();

        websocket.text(true);

        websocket.async_write(
            net::buffer(outQueue.front()),
            net::bind_executor(
                websocket.get_executor(),
                [self](beast::error_code errorCode, std::size_t) {
                    if (errorCode) {
                        self->close();
                        return;
                    }

                    if (!self->outQueue.empty()) {
                        self->outQueue.pop_front();
                    }

                    if (self->outQueue.empty()) {
                        self->writing = false;
                        return;
                    }

                    self->doWrite();
                }));
    }

    void WebSocketServer::Session::close() {
        auto self = shared_from_this();

        net::post(
            websocket.get_executor(),
            [self]() {
                if (self->closing) {
                    return;
                }

                self->closing = true;
                const bool wasOpen = self->open;
                self->open = false;
                self->writing = false;
                self->outQueue.clear();

                if (self->owner) {
                    self->owner->unregisterSession(self.get());

                    if (wasOpen && self->owner->onDisconnected_) {
                        self->owner->onDisconnected_(self.get());
                    }
                }

                beast::error_code ignored;
                self->websocket.next_layer().shutdown(tcp::socket::shutdown_both, ignored);
                self->websocket.next_layer().close(ignored);
            });
    }

    WebSocketServer::WebSocketServer(boost::asio::io_context& context, unsigned short port)
        : context_(context),
        port_(port) {
    }

    WebSocketServer::~WebSocketServer() {
        stop();
    }

    bool WebSocketServer::start() {
        if (running_) {
            return true;
        }

        beast::error_code errorCode;
        tcp::endpoint endpoint(tcp::v4(), port_);
        acceptor_ = std::make_unique<tcp::acceptor>(context_);

        acceptor_->open(endpoint.protocol(), errorCode);
        if (errorCode) {
            std::cerr << "[MediasoupWS] open failed: " << errorCode.message() << '\n';
            return false;
        }

        acceptor_->set_option(boost::asio::socket_base::reuse_address(true), errorCode);
        if (errorCode) {
            std::cerr << "[MediasoupWS] reuse_address failed: " << errorCode.message() << '\n';
            return false;
        }

        acceptor_->bind(endpoint, errorCode);
        if (errorCode) {
            std::cerr << "[MediasoupWS] bind failed: " << errorCode.message() << '\n';
            return false;
        }

        acceptor_->listen(boost::asio::socket_base::max_listen_connections, errorCode);
        if (errorCode) {
            std::cerr << "[MediasoupWS] listen failed: " << errorCode.message() << '\n';
            return false;
        }

        running_ = true;
        doAccept();
        return true;
    }

    void WebSocketServer::stop() {
        if (!running_) {
            return;
        }

        running_ = false;

        if (acceptor_) {
            beast::error_code errorCode;
            acceptor_->close(errorCode);
            acceptor_.reset();
        }

        std::vector<std::shared_ptr<Session>> aliveSessions;
        {
            std::lock_guard<std::mutex> lock(sessionsMutex_);
            aliveSessions.reserve(sessions_.size());

            for (auto& [_, weakSession] : sessions_) {
                if (auto session = weakSession.lock()) {
                    aliveSessions.push_back(std::move(session));
                }
            }

            sessions_.clear();
        }

        for (auto& session : aliveSessions) {
            session->close();
        }
    }

    bool WebSocketServer::sendText(void* session, const std::string& text) {
        std::shared_ptr<Session> wsSession;

        {
            std::lock_guard<std::mutex> lock(sessionsMutex_);
            auto iterator = sessions_.find(session);
            if (iterator == sessions_.end()) {
                return false;
            }

            wsSession = iterator->second.lock();
        }

        if (!wsSession) {
            return false;
        }

        wsSession->enqueueText(text);
        return true;
    }

    void WebSocketServer::doAccept() {
        if (!running_ || !acceptor_) {
            return;
        }

        acceptor_->async_accept(
            net::make_strand(context_),
            [this](beast::error_code errorCode, tcp::socket socket) {
                if (!errorCode) {
                    auto session = std::make_shared<Session>(std::move(socket), this);
                    session->start();
                }
                else if (running_) {
                    std::cerr << "[MediasoupWS] accept failed: " << errorCode.message() << '\n';
                }

                doAccept();
            });
    }

    void WebSocketServer::registerSession(void* key, std::shared_ptr<Session> session) {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        sessions_[key] = std::move(session);
    }

    void WebSocketServer::unregisterSession(void* key) {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        sessions_.erase(key);
    }

} // namespace eds::server_new::mediasoup::transport