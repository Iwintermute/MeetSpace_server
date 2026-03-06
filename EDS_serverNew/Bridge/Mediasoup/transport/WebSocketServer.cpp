#include "Bridge/Mediasoup/transport/WebSocketServer.h"

#include <iostream>

namespace eds::server_new::mediasoup::transport {

namespace beast = boost::beast;
namespace websocket = beast::websocket;
using tcp = boost::asio::ip::tcp;

WebSocketServer::Session::Session(tcp::socket&& socket, WebSocketServer* ownerIn)
    : websocket(std::move(socket)),
      owner(ownerIn) {
}

void WebSocketServer::Session::start() {
    auto self = shared_from_this();
    websocket.async_accept([self](beast::error_code errorCode) {
        if (errorCode) {
            std::cerr << "[MediasoupWS] accept failed: " << errorCode.message() << '\n';
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
    });
}

void WebSocketServer::Session::doRead() {
    auto self = shared_from_this();
    websocket.async_read(buffer, [self](beast::error_code errorCode, std::size_t bytesRead) {
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
    });
}

void WebSocketServer::Session::enqueueText(std::string text) {
    if (!open) {
        return;
    }

    auto self = shared_from_this();
    boost::asio::post(websocket.get_executor(), [self, text = std::move(text)]() mutable {
        if (!self->open) {
            return;
        }

        self->outQueue.push_back(std::move(text));
        if (!self->writing) {
            self->writing = true;
            self->doWrite();
        }
    });
}

void WebSocketServer::Session::doWrite() {
    auto self = shared_from_this();
    if (!open || outQueue.empty()) {
        writing = false;
        return;
    }

    websocket.text(true);
    const std::string& payload = outQueue.front();
    websocket.async_write(boost::asio::buffer(payload), [self](beast::error_code errorCode, std::size_t) {
        if (errorCode) {
            self->close();
            return;
        }

        self->outQueue.pop_front();
        self->doWrite();
    });
}

void WebSocketServer::Session::close() {
    if (!open) {
        return;
    }
    open = false;

    if (owner) {
        owner->unregisterSession(this);
        if (owner->onDisconnected_) {
            owner->onDisconnected_(this);
        }
    }

    beast::error_code errorCode;
    websocket.close(websocket::close_code::normal, errorCode);
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

    std::lock_guard<std::mutex> lock(sessionsMutex_);
    sessions_.clear();
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

    acceptor_->async_accept([this](beast::error_code errorCode, tcp::socket socket) {
        if (!errorCode) {
            auto session = std::make_shared<Session>(std::move(socket), this);
            session->start();
        } else {
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
