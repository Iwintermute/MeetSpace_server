#include "Bridge/Mediasoup/transport/WebSocketServer.h"

#include <boost/beast/ssl.hpp>
#include <openssl/err.h>
#include <openssl/ssl.h>

#include <iostream>
#include <sstream>
#include <utility>

namespace eds::server_new::mediasoup::transport {

    namespace net = boost::asio;
    namespace beast = boost::beast;
    namespace websocket = beast::websocket;
    using tcp = boost::asio::ip::tcp;

    namespace {
        constexpr std::size_t kMaxOutboundQueueMessages = 1024;
        constexpr std::size_t kMaxInboundMessageBytes = 512 * 1024;
        constexpr std::size_t kWriteBufferBytes = 256 * 1024;
        constexpr auto kTlsHandshakeTimeout = std::chrono::seconds(10);

        std::string collectOpenSslErrorChain() {
            std::ostringstream chain;
            unsigned long code;
            bool first = true;
            while ((code = ERR_get_error()) != 0) {
                char buf[256];
                ERR_error_string_n(code, buf, sizeof(buf));
                if (!first) chain << " -> ";
                chain << buf;
                first = false;
            }
            return chain.str();
        }
    }

    WebSocketServer::Session::Session(tcp::socket&& socket, WebSocketServer* ownerIn)
        : websocket(std::move(socket)),
        owner(ownerIn) {
    }

    void WebSocketServer::Session::start() {
        auto self = std::static_pointer_cast<Session>(shared_from_this());

        net::dispatch(websocket.get_executor(), [self]() {
            self->doAccept();
            });
    }

    void WebSocketServer::Session::doAccept() {
        auto self = std::static_pointer_cast<Session>(shared_from_this());

        websocket.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
        websocket::permessage_deflate permessageDeflate;
        permessageDeflate.client_enable = false;
        permessageDeflate.server_enable = false;
        websocket.set_option(permessageDeflate);
        websocket.auto_fragment(false);
        websocket.read_message_max(kMaxInboundMessageBytes);
        websocket.write_buffer_bytes(kWriteBufferBytes);

        beast::error_code noDelayError;
        websocket.next_layer().set_option(tcp::no_delay(true), noDelayError);

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

        auto self = std::static_pointer_cast<Session>(shared_from_this());

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
        auto self = std::static_pointer_cast<Session>(shared_from_this());

        net::post(
            websocket.get_executor(),
            [self, text = std::move(text)]() mutable {
                if (!self->open || self->closing) {
                    return;
                }
                if (self->outQueue.size() >= kMaxOutboundQueueMessages) {
                    self->close();
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

        auto self = std::static_pointer_cast<Session>(shared_from_this());

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
        auto self = std::static_pointer_cast<Session>(shared_from_this());

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

                auto* owner = self->owner;
                self->owner = nullptr;

                if (owner) {
                    owner->unregisterSession(self.get());

                    if (wasOpen && owner->onDisconnected_) {
                        owner->onDisconnected_(self.get());
                    }
                }

                beast::error_code ignored;
                self->websocket.next_layer().shutdown(tcp::socket::shutdown_both, ignored);
                self->websocket.next_layer().close(ignored);
            });
    }

    void WebSocketServer::Session::detachOwner() {
        owner = nullptr;
    }

    WebSocketServer::TlsSession::TlsSession(
        tcp::socket&& socket,
        WebSocketServer* ownerIn,
        std::shared_ptr<ssl_context> tlsContextIn)
        : websocket(std::move(socket), *tlsContextIn),
        tlsContext(std::move(tlsContextIn)),
        owner(ownerIn) {
    }

    void WebSocketServer::TlsSession::start() {
        auto self = std::static_pointer_cast<TlsSession>(shared_from_this());

        net::dispatch(websocket.get_executor(), [self]() {
            self->doTlsHandshake();
            });
    }

    void WebSocketServer::TlsSession::doTlsHandshake() {
        auto self = std::static_pointer_cast<TlsSession>(shared_from_this());

        beast::error_code noDelayError;
        beast::get_lowest_layer(websocket).set_option(tcp::no_delay(true), noDelayError);

        auto timer = std::make_shared<net::steady_timer>(
            beast::get_lowest_layer(websocket).get_executor(), kTlsHandshakeTimeout);
        timer->async_wait([self, timer](beast::error_code timerEc) {
            if (!timerEc) {
                beast::error_code ignored;
                beast::get_lowest_layer(self->websocket).shutdown(tcp::socket::shutdown_both, ignored);
                beast::get_lowest_layer(self->websocket).close(ignored);
            }
        });

        websocket.next_layer().async_handshake(
            net::ssl::stream_base::server,
            net::bind_executor(
                websocket.get_executor(),
                [self, timer](beast::error_code errorCode) {
                    timer->cancel();

                    if (errorCode) {
                        auto sslErrors = collectOpenSslErrorChain();
                        std::cerr << "[MediasoupWS] tls handshake failed: " << errorCode.message();
                        if (!sslErrors.empty()) {
                            std::cerr << " [ssl: " << sslErrors << ']';
                        }
                        std::cerr << '\n';
                        self->close();
                        return;
                    }

                    self->doAccept();
                }));
    }

    void WebSocketServer::TlsSession::doAccept() {
        auto self = std::static_pointer_cast<TlsSession>(shared_from_this());

        websocket.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
        websocket::permessage_deflate permessageDeflate;
        permessageDeflate.client_enable = false;
        permessageDeflate.server_enable = false;
        websocket.set_option(permessageDeflate);
        websocket.auto_fragment(false);
        websocket.read_message_max(kMaxInboundMessageBytes);
        websocket.write_buffer_bytes(kWriteBufferBytes);

        websocket.async_accept(
            net::bind_executor(
                websocket.get_executor(),
                [self](beast::error_code errorCode) {
                    if (errorCode) {
                        std::cerr << "[MediasoupWS] tls websocket accept failed: " << errorCode.message() << '\n';
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

    void WebSocketServer::TlsSession::doRead() {
        if (!open || closing) {
            return;
        }

        auto self = std::static_pointer_cast<TlsSession>(shared_from_this());

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

    void WebSocketServer::TlsSession::enqueueText(std::string text) {
        auto self = std::static_pointer_cast<TlsSession>(shared_from_this());

        net::post(
            websocket.get_executor(),
            [self, text = std::move(text)]() mutable {
                if (!self->open || self->closing) {
                    return;
                }
                if (self->outQueue.size() >= kMaxOutboundQueueMessages) {
                    self->close();
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

    void WebSocketServer::TlsSession::doWrite() {
        if (!open || closing || outQueue.empty()) {
            writing = false;
            return;
        }

        auto self = std::static_pointer_cast<TlsSession>(shared_from_this());

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

    void WebSocketServer::TlsSession::close() {
        auto self = std::static_pointer_cast<TlsSession>(shared_from_this());

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

                auto* owner = self->owner;
                self->owner = nullptr;

                if (owner) {
                    owner->unregisterSession(self.get());

                    if (wasOpen && owner->onDisconnected_) {
                        owner->onDisconnected_(self.get());
                    }
                }

                beast::error_code ignored;
                self->websocket.next_layer().shutdown(ignored);
                beast::get_lowest_layer(self->websocket).shutdown(tcp::socket::shutdown_both, ignored);
                beast::get_lowest_layer(self->websocket).close(ignored);
            });
    }

    void WebSocketServer::TlsSession::detachOwner() {
        owner = nullptr;
    }

    WebSocketServer::WebSocketServer(
        boost::asio::io_context& context,
        unsigned short port,
        WebSocketTlsOptions tlsOptions)
        : context_(context),
        port_(port),
        tlsOptions_(std::move(tlsOptions)) {
    }

    WebSocketServer::~WebSocketServer() {
        stop();
    }

    bool WebSocketServer::configureTlsContext() {
        if (!tlsOptions_.enabled) {
            tlsContext_.reset();
            return true;
        }

        if (tlsOptions_.certificateChainFile.empty() || tlsOptions_.privateKeyFile.empty()) {
            std::cerr << "[MediasoupWS] TLS is enabled but certificate or private key file is missing.\n";
            return false;
        }

        tlsContext_ = std::make_shared<net::ssl::context>(net::ssl::context::tls_server);
        beast::error_code errorCode;

        tlsContext_->set_options(
            net::ssl::context::default_workarounds
            | net::ssl::context::no_sslv2
            | net::ssl::context::no_sslv3
            | net::ssl::context::no_tlsv1
            | net::ssl::context::no_tlsv1_1
            | net::ssl::context::single_dh_use,
            errorCode);
        if (errorCode) {
            std::cerr << "[MediasoupWS] tls set_options failed: " << errorCode.message() << '\n';
            return false;
        }

        SSL_CTX_set_options(tlsContext_->native_handle(), SSL_OP_NO_RENEGOTIATION);

        if (SSL_CTX_set_cipher_list(tlsContext_->native_handle(),
                "ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:"
                "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:"
                "ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-RSA-CHACHA20-POLY1305") != 1) {
            std::cerr << "[MediasoupWS] tls cipher list configuration warning (non-fatal).\n";
        }
        if (SSL_CTX_set_ciphersuites(tlsContext_->native_handle(),
                "TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256:TLS_AES_128_GCM_SHA256") != 1) {
            std::cerr << "[MediasoupWS] tls 1.3 ciphersuites configuration warning (non-fatal).\n";
        }

        tlsContext_->use_certificate_chain_file(tlsOptions_.certificateChainFile, errorCode);
        if (errorCode) {
            std::cerr << "[MediasoupWS] tls certificate load failed: " << errorCode.message() << '\n';
            return false;
        }

        tlsContext_->use_private_key_file(
            tlsOptions_.privateKeyFile,
            net::ssl::context::file_format::pem,
            errorCode);
        if (errorCode) {
            std::cerr << "[MediasoupWS] tls private key load failed: " << errorCode.message() << '\n';
            return false;
        }

        if (!tlsOptions_.dhParamsFile.empty()) {
            tlsContext_->use_tmp_dh_file(tlsOptions_.dhParamsFile, errorCode);
            if (errorCode) {
                std::cerr << "[MediasoupWS] tls dh params load failed: " << errorCode.message() << '\n';
                return false;
            }
        }

        if (tlsOptions_.requireClientCertificate) {
            if (tlsOptions_.caCertificateFile.empty()) {
                std::cerr << "[MediasoupWS] tls client certificate verification requires CA certificate file.\n";
                return false;
            }

            tlsContext_->load_verify_file(tlsOptions_.caCertificateFile, errorCode);
            if (errorCode) {
                std::cerr << "[MediasoupWS] tls CA certificate load failed: " << errorCode.message() << '\n';
                return false;
            }

            tlsContext_->set_verify_mode(
                net::ssl::verify_peer | net::ssl::verify_fail_if_no_peer_cert,
                errorCode);
            if (errorCode) {
                std::cerr << "[MediasoupWS] tls set_verify_mode failed: " << errorCode.message() << '\n';
                return false;
            }
        }
        else {
            tlsContext_->set_verify_mode(net::ssl::verify_none, errorCode);
            if (errorCode) {
                std::cerr << "[MediasoupWS] tls set_verify_mode failed: " << errorCode.message() << '\n';
                return false;
            }
        }

        return true;
    }

    bool WebSocketServer::start() {
        if (running_) {
            return true;
        }

        if (!configureTlsContext()) {
            return false;
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
        tlsContext_.reset();

        std::vector<std::shared_ptr<SessionHandle>> aliveSessions;
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
            session->detachOwner();
            session->close();
        }
    }

    bool WebSocketServer::sendText(void* session, const std::string& text) {
        std::shared_ptr<SessionHandle> wsSession;

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

    std::size_t WebSocketServer::sendTexts(const std::vector<void*>& sessions, const std::string& text) {
        if (sessions.empty() || text.empty()) {
            return 0;
        }

        std::vector<std::shared_ptr<SessionHandle>> aliveSessions;
        aliveSessions.reserve(sessions.size());

        {
            std::lock_guard<std::mutex> lock(sessionsMutex_);
            for (auto* sessionKey : sessions) {
                if (sessionKey == nullptr) {
                    continue;
                }

                auto iterator = sessions_.find(sessionKey);
                if (iterator == sessions_.end()) {
                    continue;
                }

                auto wsSession = iterator->second.lock();
                if (!wsSession) {
                    continue;
                }

                aliveSessions.push_back(std::move(wsSession));
            }
        }

        if (aliveSessions.empty()) {
            return 0;
        }

        for (auto& wsSession : aliveSessions) {
            wsSession->enqueueText(text);
        }

        return aliveSessions.size();
    }

    void WebSocketServer::doAccept() {
        if (!running_ || !acceptor_) {
            return;
        }

        acceptor_->async_accept(
            net::make_strand(context_),
            [this](beast::error_code errorCode, tcp::socket socket) {
                if (!errorCode) {
                    std::shared_ptr<SessionHandle> session;
                    if (tlsOptions_.enabled) {
                        if (!tlsContext_) {
                            std::cerr << "[MediasoupWS] TLS context is not initialized.\n";
                        }
                        else {
                            session = std::make_shared<TlsSession>(std::move(socket), this, tlsContext_);
                        }
                    }
                    else {
                        session = std::make_shared<Session>(std::move(socket), this);
                    }

                    if (session) {
                        session->start();
                    }
                }
                else if (running_) {
                    std::cerr << "[MediasoupWS] accept failed: " << errorCode.message() << '\n';
                }

                doAccept();
            });
    }

    void WebSocketServer::registerSession(void* key, std::shared_ptr<SessionHandle> session) {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        sessions_[key] = std::move(session);
    }

    void WebSocketServer::unregisterSession(void* key) {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        sessions_.erase(key);
    }

} // namespace eds::server_new::mediasoup::transport
