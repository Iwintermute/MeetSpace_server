#include "Bridge/Mediasoup/service/MediasoupSfuEngine.h"

#include <boost/asio/ip/address.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/system/system_error.hpp>
#include <nlohmann/json.hpp>
#include <openssl/ssl.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <unordered_set>
#include <utility>

namespace eds::server_new::mediasoup::service {
    namespace {
        using json = nlohmann::json;

        std::string makeRequestId(std::string_view correlationId) {
            if (!correlationId.empty()) {
                return std::string(correlationId);
            }
            const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            return std::string("media-") + std::to_string(nowMs);
        }

        std::string toLowerCopy(std::string_view value) {
            std::string result(value);
            std::transform(result.begin(), result.end(), result.begin(), [](unsigned char symbol) {
                return static_cast<char>(std::tolower(symbol));
                });
            return result;
        }

        std::string stripIpv6Brackets(std::string_view host) {
            if (host.size() >= 2 && host.front() == '[' && host.back() == ']') {
                return std::string(host.substr(1, host.size() - 2));
            }
            return std::string(host);
        }

        bool isPrivateOrLoopbackAddress(const boost::asio::ip::address& address) {
            if (address.is_v4()) {
                const auto bytes = address.to_v4().to_bytes();
                if (bytes[0] == 127) {
                    return true;
                }
                if (bytes[0] == 10) {
                    return true;
                }
                if (bytes[0] == 172 && bytes[1] >= 16 && bytes[1] <= 31) {
                    return true;
                }
                if (bytes[0] == 192 && bytes[1] == 168) {
                    return true;
                }
                if (bytes[0] == 169 && bytes[1] == 254) {
                    return true;
                }
                return false;
            }

            const auto addressV6 = address.to_v6();
            if (addressV6.is_loopback()) {
                return true;
            }

            const auto bytes = addressV6.to_bytes();
            if ((bytes[0] & 0xFE) == 0xFC) {
                return true;
            }
            if (bytes[0] == 0xFE && (bytes[1] & 0xC0) == 0x80) {
                return true;
            }
            return false;
        }

        json parseOptionalJsonPayload(std::string_view rawValue) {
            if (rawValue.empty()) {
                return json();
            }

            const auto parsed = json::parse(std::string(rawValue), nullptr, false);
            if (parsed.is_discarded()) {
                return json();
            }
            if (parsed.is_object() || parsed.is_array()) {
                return parsed;
            }
            return json();
        }

        json parseBackendResponse(std::string_view raw) {
            const auto parsed = json::parse(std::string(raw), nullptr, false);
            return parsed.is_object() ? parsed : json::object();
        }

        bool containsCaseInsensitive(std::string_view value, std::string_view token) {
            if (value.empty() || token.empty()) {
                return false;
            }

            const auto loweredValue = toLowerCopy(value);
            const auto loweredToken = toLowerCopy(token);
            return loweredValue.find(loweredToken) != std::string::npos;
        }
        std::string readEnvVar(const char* name) {
            const auto value = std::getenv(name);
            if (value == nullptr || value[0] == '\0') {
                return {};
            }
            return std::string(value);
        }

        std::string readFirstEnvVar(std::initializer_list<const char*> names) {
            for (const auto* name : names) {
                if (name == nullptr || name[0] == '\0') {
                    continue;
                }

                auto value = readEnvVar(name);
                if (!value.empty()) {
                    return value;
                }
            }
            return {};
        }

        bool parseBooleanOption(std::string value, bool& result) {
            if (value.empty()) {
                return false;
            }

            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char symbol) {
                return static_cast<char>(std::tolower(symbol));
                });
            if (value == "1" || value == "true" || value == "yes" || value == "on") {
                result = true;
                return true;
            }
            if (value == "0" || value == "false" || value == "no" || value == "off") {
                result = false;
                return true;
            }
            return false;
        }

        bool readFirstBooleanEnvVar(std::initializer_list<const char*> names, bool fallbackValue) {
            for (const auto* name : names) {
                if (name == nullptr || name[0] == '\0') {
                    continue;
                }

                auto value = readEnvVar(name);
                if (value.empty()) {
                    continue;
                }

                bool parsed = fallbackValue;
                if (parseBooleanOption(value, parsed)) {
                    return parsed;
                }
            }
            return fallbackValue;
        }

    } // namespace

    MediasoupSfuEngine::MediasoupSfuEngine(bool debugMode)
        : debugMode_(debugMode),
        backendUrl_(defaultBackendUrl()) {
        refreshBackendEndpointNoLock();
        resolver_ = std::make_unique<tcp::resolver>(ioContext_);
    }

    MediasoupSfuEngine::~MediasoupSfuEngine() {
        std::lock_guard<std::mutex> lock(mutex_);
        disconnectNoLock();
    }

    core::contracts::OperationStatus MediasoupSfuEngine::execute(
        MediaTransportIntent intent,
        const MediaTransportCommand& command,
        std::vector<MediaTransportEvent>& emittedEvents) {
        std::lock_guard<std::mutex> lock(mutex_);
        return executeIntentNoLock(intent, command, emittedEvents);
    }

    std::vector<MediaSignalingEvent> MediasoupSfuEngine::consumeSignalingEventsForPeer(std::string_view peerId) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto iterator = signalingByPeer_.find(std::string(peerId));
        if (iterator == signalingByPeer_.end()) {
            return {};
        }

        auto events = std::move(iterator->second);
        signalingByPeer_.erase(iterator);
        return events;
    }

    void MediasoupSfuEngine::onSessionDisconnected(
        std::string_view peerId,
        std::uintptr_t sessionHandle,
        std::vector<MediaTransportEvent>& emittedEvents) {
        std::lock_guard<std::mutex> lock(mutex_);
        emittedEvents.clear();
        if (peerId.empty()) {
            return;
        }

        auto peerIt = peers_.find(std::string(peerId));
        if (peerIt == peers_.end()) {
            signalingByPeer_.erase(std::string(peerId));
            return;
        }
        if (sessionHandle != 0 && peerIt->second.sessionHandle != 0 && peerIt->second.sessionHandle != sessionHandle) {
            return;
        }

        MediaTransportCommand command;
        command.sessionHandle = sessionHandle;
        command.sessionId = std::string(peerId);
        command.peerId = std::string(peerId);
        command.roomId = peerIt->second.roomId;
        command.correlationId = makeRequestId("session_disconnected");
        static_cast<void>(leaveSessionNoLock(command, emittedEvents, "disconnected", "router.closePeer"));
    }

    core::contracts::OperationStatus MediasoupSfuEngine::requireField(bool condition, std::string fieldName) {
        if (!condition) {
            return core::contracts::OperationStatus::failure(std::move(fieldName) + " must not be empty.");
        }
        return core::contracts::OperationStatus::success();
    }

    MediaTransportEvent MediasoupSfuEngine::makeErrorEvent(const MediaTransportCommand& command, std::string reason) {
        MediaTransportEvent event;
        event.type = MediaTransportEventType::TransportError;
        event.correlationId = command.correlationId;
        event.peerId = command.peerId;
        event.roomId = command.roomId;
        event.transportId = command.transportId;
        event.producerId = command.producerId;
        event.consumerId = command.consumerId;
        event.reason = std::move(reason);
        return event;
    }

    bool MediasoupSfuEngine::parseBackendUrl(
        std::string_view url,
        std::string& host,
        std::string& port,
        std::string& path,
        bool& secure) {
        constexpr std::string_view wsPrefix = "ws://";
        constexpr std::string_view wssPrefix = "wss://";

        std::string_view remaining;
        if (url.rfind(wsPrefix, 0) == 0) {
            remaining = url.substr(wsPrefix.size());
            secure = false;
        }
        else if (url.rfind(wssPrefix, 0) == 0) {
            remaining = url.substr(wssPrefix.size());
            secure = true;
        }
        else {
            return false;
        }
        const auto slashPos = remaining.find('/');
        const auto hostPort = slashPos == std::string_view::npos ? remaining : remaining.substr(0, slashPos);
        path = slashPos == std::string_view::npos ? "/" : std::string(remaining.substr(slashPos));

        const auto colonPos = hostPort.rfind(':');
        if (colonPos == std::string_view::npos) {
            host = std::string(hostPort);
            port = secure ? "443" : "80";
        }
        else {
            host = std::string(hostPort.substr(0, colonPos));
            port = std::string(hostPort.substr(colonPos + 1));
        }
        if (host.empty() || port.empty()) {
            return false;
        }
        if (path.empty()) {
            path = "/";
        }
        return true;
    }

    bool MediasoupSfuEngine::isPrivateOrLoopbackHost(std::string_view host) {
        if (host.empty()) {
            return false;
        }

        const auto normalizedHost = stripIpv6Brackets(host);
        if (normalizedHost.empty()) {
            return false;
        }

        if (toLowerCopy(normalizedHost) == "localhost") {
            return true;
        }

        boost::system::error_code addressCode;
        const auto parsedAddress = boost::asio::ip::make_address(normalizedHost, addressCode);
        if (addressCode) {
            return false;
        }

        return isPrivateOrLoopbackAddress(parsedAddress);
    }

    std::string MediasoupSfuEngine::defaultBackendUrl() {
        const auto meetspaceValue = std::getenv("MEETSPACE_MEDIASOUP_BACKEND_URL");
        if (meetspaceValue != nullptr && meetspaceValue[0] != '\0') {
            return std::string(meetspaceValue);
        }
        const auto envValue = std::getenv("EDUSPACE_MEDIASOUP_BACKEND_URL");
        if (envValue != nullptr && envValue[0] != '\0') {
            return std::string(envValue);
        }
        return {};
    }

    std::string MediasoupSfuEngine::resolveOperationName(MediaTransportIntent intent) {
        switch (intent) {
        case MediaTransportIntent::CreateRoom:
            return "worker.createRouter";
        case MediaTransportIntent::JoinSession:
            return "router.joinPeer";
        case MediaTransportIntent::LeaveSession:
            return "router.leavePeer";
        case MediaTransportIntent::OpenTransport:
            return "router.createWebRtcTransport";
        case MediaTransportIntent::PublishTrack:
            return "transport.produce";
        case MediaTransportIntent::PauseTrack:
            return "producer.pause";
        case MediaTransportIntent::ResumeTrack:
            return "producer.resume";
        case MediaTransportIntent::CloseTrack:
            return "producer.close";
        case MediaTransportIntent::ConsumeTrack:
            return "transport.consume";
        case MediaTransportIntent::ResumeConsumer:
            return "consumer.resume";
        case MediaTransportIntent::ApplyOffer:
            return "transport.connectDtls";
        case MediaTransportIntent::ApplyIce:
            return "noop";
        case MediaTransportIntent::ReadStats:
            return "system.getMediaStats";
        case MediaTransportIntent::CloseSession:
            return "router.closePeer";
        }
        return "unknown";
    }

    bool MediasoupSfuEngine::isMediasoupEngineName(std::string_view value) {
        if (value.empty()) {
            return false;
        }
        return toLowerCopy(value).find("mediasoup") != std::string::npos;
    }

    void MediasoupSfuEngine::refreshBackendEndpointNoLock() {
        const auto resolvedBackendUrl = defaultBackendUrl();
        std::string parsedHost;
        std::string parsedPort;
        std::string parsedPath = "/";
        bool parsedSecure = false;
        backendEndpointValidationError_.clear();
        if (!resolvedBackendUrl.empty()
            && !parseBackendUrl(resolvedBackendUrl, parsedHost, parsedPort, parsedPath, parsedSecure)) {
            if (backendConnected_ || backendVerified_) {
                disconnectNoLock();
            }
            backendUrl_ = resolvedBackendUrl;
            backendEndpointValidationError_ =
                "Invalid mediasoup backend endpoint. Expected ws://host:port/path or wss://host:port/path.";
            backendHost_.clear();
            backendPort_.clear();
            backendPath_ = "/";
            backendSecure_ = false;
            backendTlsInsecureSkipVerify_ = false;
            backendTlsCaFile_.clear();
            backendTlsServerName_.clear();
            return;
        }
        if (!resolvedBackendUrl.empty() && !parsedSecure && !isPrivateOrLoopbackHost(parsedHost)) {
            if (backendConnected_ || backendVerified_) {
                disconnectNoLock();
            }
            backendUrl_ = resolvedBackendUrl;
            backendEndpointValidationError_ =
                "Remote mediasoup backend endpoint must use wss://. ws:// is allowed only for localhost/private hosts.";
            backendHost_.clear();
            backendPort_.clear();
            backendPath_ = "/";
            backendSecure_ = false;
            backendTlsInsecureSkipVerify_ = false;
            backendTlsCaFile_.clear();
            backendTlsServerName_.clear();
            return;
        }
        const bool parsedTlsInsecureSkipVerify = readFirstBooleanEnvVar(
            {
                "MEETSPACE_MEDIASOUP_BACKEND_TLS_INSECURE_SKIP_VERIFY",
                "EDUSPACE_MEDIASOUP_BACKEND_TLS_INSECURE_SKIP_VERIFY",
                "MEDIASOUP_BACKEND_TLS_INSECURE_SKIP_VERIFY"
            },
            false);
        const auto parsedTlsCaFile = readFirstEnvVar(
            {
                "MEETSPACE_MEDIASOUP_BACKEND_TLS_CA_FILE",
                "EDUSPACE_MEDIASOUP_BACKEND_TLS_CA_FILE",
                "MEDIASOUP_BACKEND_TLS_CA_FILE"
            });
        auto parsedTlsServerName = readFirstEnvVar(
            {
                "MEETSPACE_MEDIASOUP_BACKEND_TLS_SERVER_NAME",
                "EDUSPACE_MEDIASOUP_BACKEND_TLS_SERVER_NAME",
                "MEDIASOUP_BACKEND_TLS_SERVER_NAME"
            });
        if (parsedTlsServerName.empty()) {
            parsedTlsServerName = parsedHost;
        }

        const bool connectionSettingsChanged =
            resolvedBackendUrl != backendUrl_
            || parsedHost != backendHost_
            || parsedPort != backendPort_
            || parsedPath != backendPath_
            || parsedSecure != backendSecure_
            || parsedTlsInsecureSkipVerify != backendTlsInsecureSkipVerify_
            || parsedTlsCaFile != backendTlsCaFile_
            || parsedTlsServerName != backendTlsServerName_;
        if (connectionSettingsChanged && (backendConnected_ || backendVerified_)) {
            disconnectNoLock();
        }

        backendUrl_ = resolvedBackendUrl;
        backendHost_ = std::move(parsedHost);
        backendPort_ = std::move(parsedPort);
        backendPath_ = std::move(parsedPath);
        backendSecure_ = parsedSecure;
        backendTlsInsecureSkipVerify_ = parsedTlsInsecureSkipVerify;
        backendTlsCaFile_ = parsedTlsCaFile;
        backendTlsServerName_ = std::move(parsedTlsServerName);

        if (backendHost_.empty() || backendPort_.empty()) {
            backendPath_ = "/";
            backendSecure_ = false;
            backendTlsServerName_.clear();
        }
    }

    core::contracts::OperationStatus MediasoupSfuEngine::configureBackendTlsContextNoLock() {
        if (!backendSecure_) {
            tlsContext_.reset();
            return core::contracts::OperationStatus::success();
        }

        tlsContext_ = std::make_unique<boost::asio::ssl::context>(boost::asio::ssl::context::tls_client);
        boost::system::error_code code;

        tlsContext_->set_options(
            boost::asio::ssl::context::default_workarounds
            | boost::asio::ssl::context::no_sslv2
            | boost::asio::ssl::context::no_sslv3
            | boost::asio::ssl::context::no_tlsv1
            | boost::asio::ssl::context::no_tlsv1_1,
            code);
        if (code) {
            return core::contracts::OperationStatus::failure(
                std::string("Failed to configure mediasoup backend TLS options: ") + code.message());
        }

        if (!backendTlsInsecureSkipVerify_) {
            if (!backendTlsCaFile_.empty()) {
                tlsContext_->load_verify_file(backendTlsCaFile_, code);
                if (code) {
                    return core::contracts::OperationStatus::failure(
                        std::string("Failed to load mediasoup backend TLS CA file: ") + code.message());
                }
            }
            else {
                tlsContext_->set_default_verify_paths(code);
                if (code) {
                    return core::contracts::OperationStatus::failure(
                        std::string("Failed to configure mediasoup backend TLS trust store: ") + code.message());
                }
            }

            tlsContext_->set_verify_mode(boost::asio::ssl::verify_peer, code);
            if (code) {
                return core::contracts::OperationStatus::failure(
                    std::string("Failed to enable mediasoup backend TLS peer verification: ") + code.message());
            }
        }
        else {
            tlsContext_->set_verify_mode(boost::asio::ssl::verify_none, code);
            if (code) {
                return core::contracts::OperationStatus::failure(
                    std::string("Failed to configure mediasoup backend TLS verify mode: ") + code.message());
            }
        }

        return core::contracts::OperationStatus::success();
    }

    core::contracts::OperationStatus MediasoupSfuEngine::sendBackendRequestNoLock(
        const std::string& serializedRequest,
        std::string& responseText) {
        if (serializedRequest.empty()) {
            return core::contracts::OperationStatus::failure("Mediasoup backend request payload is empty.");
        }

        const auto timeout = std::chrono::milliseconds(
            std::max(500, mediaPolicy_.backendOperationTimeoutMs()));

        try {
            boost::beast::flat_buffer responseBuffer;
            if (backendSecure_) {
                if (!tlsSocket_) {
                    return core::contracts::OperationStatus::failure(
                        "Mediasoup backend TLS websocket is not initialized.");
                }

                auto& lowestLayer = boost::beast::get_lowest_layer(*tlsSocket_);
                lowestLayer.expires_after(timeout);
                tlsSocket_->write(boost::asio::buffer(serializedRequest));

                lowestLayer.expires_after(timeout);
                tlsSocket_->read(responseBuffer);
            }
            else {
                if (!socket_) {
                    return core::contracts::OperationStatus::failure(
                        "Mediasoup backend websocket is not initialized.");
                }

                auto& lowestLayer = boost::beast::get_lowest_layer(*socket_);
                lowestLayer.expires_after(timeout);
                socket_->write(boost::asio::buffer(serializedRequest));

                lowestLayer.expires_after(timeout);
                socket_->read(responseBuffer);
            }

            responseText = boost::beast::buffers_to_string(responseBuffer.data());
            return core::contracts::OperationStatus::success();
        }
        catch (const boost::system::system_error& error) {
            return core::contracts::OperationStatus::failure(
                std::string("Mediasoup backend websocket IO failed: ") + error.what());
        }
        catch (const std::exception& error) {
            return core::contracts::OperationStatus::failure(
                std::string("Mediasoup backend websocket IO failed: ") + error.what());
        }
    }

    core::contracts::OperationStatus MediasoupSfuEngine::ensureConnectedNoLock() {
        refreshBackendEndpointNoLock();
        if (backendConnected_ && backendVerified_) {
            return core::contracts::OperationStatus::success();
        }
        if (backendConnected_ && !backendVerified_) {
            return verifyMediasoupBackendNoLock();
        }
        if (backendHost_.empty() || backendPort_.empty()) {
            if (!backendEndpointValidationError_.empty()) {
                return core::contracts::OperationStatus::failure(backendEndpointValidationError_);
            }
            return core::contracts::OperationStatus::failure(
                "Mediasoup backend endpoint is not configured. Set MEETSPACE_MEDIASOUP_BACKEND_URL.");
        }

        try {
            if (!resolver_) {
                resolver_ = std::make_unique<tcp::resolver>(ioContext_);
            }

            const auto endpoints = resolver_->resolve(backendHost_, backendPort_);
            const auto connectTimeout = std::chrono::milliseconds(
                std::max(500, mediaPolicy_.backendConnectTimeoutMs()));

            if (backendSecure_) {
                socket_.reset();

                const auto tlsContextStatus = configureBackendTlsContextNoLock();
                if (!tlsContextStatus.ok) {
                    return tlsContextStatus;
                }
                if (!tlsContext_) {
                    return core::contracts::OperationStatus::failure(
                        "Mediasoup backend TLS context is not initialized.");
                }

                tlsSocket_ = std::make_unique<wss_stream>(ioContext_, *tlsContext_);
                auto& lowestLayer = boost::beast::get_lowest_layer(*tlsSocket_);
                lowestLayer.expires_after(connectTimeout);
                lowestLayer.connect(endpoints);

                if (!backendTlsServerName_.empty()
                    && !SSL_set_tlsext_host_name(
                        tlsSocket_->next_layer().native_handle(),
                        backendTlsServerName_.c_str())) {
                    disconnectNoLock();
                    return core::contracts::OperationStatus::failure(
                        "Failed to configure TLS SNI for mediasoup backend connection.");
                }

                lowestLayer.expires_after(connectTimeout);
                tlsSocket_->next_layer().handshake(boost::asio::ssl::stream_base::client);

                lowestLayer.expires_after(connectTimeout);
                tlsSocket_->handshake(backendHost_ + ":" + backendPort_, backendPath_);
            }
            else {
                tlsSocket_.reset();
                tlsContext_.reset();
                socket_ = std::make_unique<ws_stream>(ioContext_);
                auto& lowestLayer = boost::beast::get_lowest_layer(*socket_);
                lowestLayer.expires_after(connectTimeout);
                lowestLayer.connect(endpoints);

                lowestLayer.expires_after(connectTimeout);
                socket_->handshake(backendHost_ + ":" + backendPort_, backendPath_);
            }
            backendConnected_ = true;

            const auto verificationStatus = verifyMediasoupBackendNoLock();
            if (!verificationStatus.ok) {
                disconnectNoLock();
                return verificationStatus;
            }
            return core::contracts::OperationStatus::success();
        }
        catch (const boost::system::system_error& error) {
            disconnectNoLock();
            return core::contracts::OperationStatus::failure(
                std::string("Failed to connect mediasoup backend: ") + error.what());
        }
    }

    core::contracts::OperationStatus MediasoupSfuEngine::verifyMediasoupBackendNoLock() {
        if (!backendConnected_) {
            return core::contracts::OperationStatus::failure("Mediasoup backend websocket is not connected.");
        }
        if (backendSecure_ && !tlsSocket_) {
            return core::contracts::OperationStatus::failure(
                "Mediasoup backend TLS websocket is not initialized.");
        }
        if (!backendSecure_ && !socket_) {
            return core::contracts::OperationStatus::failure(
                "Mediasoup backend websocket is not initialized.");
        }

        try {
            json request{
                { "id", makeRequestId("verify-mediasoup-backend") },
                { "operation", "system.getCapabilities" },
                { "payload", {
                    { "requireEngine", "mediasoup" },
                    { "requireRouterRtpCapabilities", true }
                } }
            };
            std::string responseText;
            const auto requestStatus = sendBackendRequestNoLock(request.dump(), responseText);
            if (!requestStatus.ok) {
                return requestStatus;
            }
            const auto response = json::parse(responseText, nullptr, false);
            if (!response.is_object()) {
                return core::contracts::OperationStatus::failure("Invalid mediasoup capability response.");
            }
            if (!response.contains("ok") || !response["ok"].is_boolean()) {
                return core::contracts::OperationStatus::failure(
                    "Mediasoup capability response must contain boolean field 'ok'.");
            }
            if (!response["ok"].get<bool>()) {
                return core::contracts::OperationStatus::failure(
                    response.value("message", std::string("Mediasoup capability check failed.")));
            }

            const auto backend = response.contains("backend") && response["backend"].is_object()
                ? response["backend"]
                : response;
            const auto engine = backend.value("engine", std::string{});
            if (!isMediasoupEngineName(engine)) {
                return core::contracts::OperationStatus::failure(
                    "Connected backend is not mediasoup. Reported engine: " + engine);
            }
            if (!backend.contains("routerRtpCapabilities") || !backend["routerRtpCapabilities"].is_object()) {
                return core::contracts::OperationStatus::failure(
                    "Mediasoup capability response has no routerRtpCapabilities.");
            }

            backendEngine_ = engine;
            backendVersion_ = backend.value("version", response.value("version", std::string{}));
            backendVerified_ = true;
            return core::contracts::OperationStatus::success();
        }
        catch (const std::exception& error) {
            return core::contracts::OperationStatus::failure(
                std::string("Mediasoup backend verification failed: ") + error.what());
        }
    }

    void MediasoupSfuEngine::disconnectNoLock() {
        if (tlsSocket_) {
            boost::system::error_code closeCode;
            tlsSocket_->close(boost::beast::websocket::close_code::normal, closeCode);

            boost::system::error_code shutdownCode;
            tlsSocket_->next_layer().shutdown(shutdownCode);
            tlsSocket_.reset();
        }
        if (socket_) {
            boost::system::error_code closeCode;
            socket_->close(boost::beast::websocket::close_code::normal, closeCode);
            socket_.reset();
        }
        backendConnected_ = false;
        backendVerified_ = false;
        backendEngine_.clear();
        backendVersion_.clear();
    }

    core::contracts::OperationStatus MediasoupSfuEngine::callMediasoupBackendNoLock(
        const std::string& operationName,
        const MediaTransportCommand& command,
        std::string& backendMessage) {
        const auto maxRetries = std::max(0, mediaPolicy_.backendMaxRetries());
        auto lastFailure = core::contracts::OperationStatus::failure(
            "Mediasoup backend request failed.");

        for (int attempt = 0; attempt <= maxRetries; ++attempt) {
            const auto connectionStatus = ensureConnectedNoLock();
            if (!connectionStatus.ok) {
                lastFailure = connectionStatus;
                disconnectNoLock();
                if (attempt < maxRetries) {
                    continue;
                }
                return lastFailure;
            }
            if (!backendVerified_) {
                lastFailure = core::contracts::OperationStatus::failure(
                    "Mediasoup backend is connected but not verified.");
                disconnectNoLock();
                if (attempt < maxRetries) {
                    continue;
                }
                return lastFailure;
            }

            try {
                json payload{
                    { "sessionHandle", command.sessionHandle },
                    { "sessionId", command.sessionId },
                    { "peerId", command.peerId },
                    { "roomId", command.roomId },
                    { "transportId", command.transportId },
                    { "producerId", command.producerId },
                    { "consumerId", command.consumerId },
                    { "kind", command.kind },
                    { "trackType", command.trackType },
                    { "sdp", command.sdp },
                    { "sdpMid", command.sdpMid },
                    { "candidate", command.candidate },
                    { "injectTestRtp", command.injectTestRtp },
                    { "correlationId", command.correlationId }
                };

                const auto dtlsParameters = parseOptionalJsonPayload(command.dtlsParameters);
                if (!dtlsParameters.is_null()) {
                    payload["dtlsParameters"] = dtlsParameters;
                }
                const auto rtpParameters = parseOptionalJsonPayload(command.rtpParameters);
                if (!rtpParameters.is_null()) {
                    payload["rtpParameters"] = rtpParameters;
                }
                const auto rtpCapabilities = parseOptionalJsonPayload(command.rtpCapabilities);
                if (!rtpCapabilities.is_null()) {
                    payload["rtpCapabilities"] = rtpCapabilities;
                }
                if (command.injectTestRtp) {
                    json testRtp = json::object();
                    if (command.testRtpPacketCount > 0) {
                        testRtp["packetCount"] = command.testRtpPacketCount;
                    }
                    if (command.testRtpPayloadSize > 0) {
                        testRtp["payloadSize"] = command.testRtpPayloadSize;
                    }
                    if (command.testRtpTimestampStep > 0) {
                        testRtp["timestampStep"] = command.testRtpTimestampStep;
                    }
                    if (!testRtp.empty()) {
                        payload["testRtp"] = std::move(testRtp);
                    }
                }

                json request{
                    { "id", makeRequestId(command.correlationId) },
                    { "operation", operationName },
                    { "requireEngine", "mediasoup" },
                    { "payload", std::move(payload) }
                };

                std::string responseText;
                const auto requestStatus = sendBackendRequestNoLock(request.dump(), responseText);
                if (!requestStatus.ok) {
                    lastFailure = requestStatus;
                    disconnectNoLock();
                    if (attempt < maxRetries) {
                        continue;
                    }
                    return lastFailure;
                }

                backendMessage = std::move(responseText);
                appendSignalingEventsFromBackendNoLock(command.peerId, backendMessage);

                const auto response = json::parse(backendMessage, nullptr, false);
                if (!response.is_object()) {
                    return core::contracts::OperationStatus::failure("Invalid mediasoup backend response format.");
                }
                if (!response.contains("ok") || !response["ok"].is_boolean()) {
                    return core::contracts::OperationStatus::failure(
                        "Mediasoup backend response must contain boolean field 'ok'.");
                }
                if (response.contains("backend") && response["backend"].is_object()) {
                    const auto engine = response["backend"].value("engine", std::string{});
                    if (!engine.empty() && !isMediasoupEngineName(engine)) {
                        disconnectNoLock();
                        return core::contracts::OperationStatus::failure(
                            "Mediasoup backend engine mismatch during operation '" + operationName + "'.");
                    }
                }
                const bool ok = response.value("ok", false);
                const auto message = response.value("message", std::string{});
                if (!ok) {
                    return core::contracts::OperationStatus::failure(
                        message.empty() ? std::string("Mediasoup backend rejected operation: ") + operationName : message);
                }

                const auto updatedCount = ++backendOperationCounters_[operationName];
                if (debugMode_) {
                    std::cout << "[mediasoup][ops] operation=" << operationName
                        << " count=" << updatedCount
                        << " engine=" << backendEngine_
                        << "\n";
                }

                return core::contracts::OperationStatus::success();
            }
            catch (const std::exception& error) {
                lastFailure = core::contracts::OperationStatus::failure(
                    std::string("Mediasoup backend request failed: ") + error.what());
                disconnectNoLock();
                if (attempt < maxRetries) {
                    continue;
                }
                return lastFailure;
            }
        }

        return lastFailure;
    }

    void MediasoupSfuEngine::appendSignalingEventsFromBackendNoLock(
        std::string_view fallbackPeerId,
        std::string_view backendPayloadText) {
        const auto response = json::parse(std::string(backendPayloadText), nullptr, false);
        if (!response.is_object() || !response.contains("signalingEvents") || !response["signalingEvents"].is_array()) {
            return;
        }

        for (const auto& item : response["signalingEvents"]) {
            if (!item.is_object()) {
                continue;
            }

            MediaSignalingEvent signalingEvent;
            signalingEvent.type = item.value("type", std::string{});
            signalingEvent.peerId = item.value("peerId", item.value("peer", std::string(fallbackPeerId)));
            signalingEvent.sdp = item.value("sdp", std::string{});
            signalingEvent.sdpMid = item.value("sdpMid", std::string{});
            signalingEvent.candidate = item.value("candidate", std::string{});
            if (signalingEvent.peerId.empty()) {
                signalingEvent.peerId = std::string(fallbackPeerId);
            }
            signalingByPeer_[signalingEvent.peerId].push_back(std::move(signalingEvent));
        }
    }

    std::vector<std::string> MediasoupSfuEngine::collectRoomPeersNoLock(std::string_view roomId) const {
        std::vector<std::string> peers;
        const auto roomIt = rooms_.find(std::string(roomId));
        if (roomIt == rooms_.end()) {
            return peers;
        }

        peers.reserve(roomIt->second.peers.size());
        for (const auto& peerId : roomIt->second.peers) {
            peers.push_back(peerId);
        }
        return peers;
    }

    std::vector<MediaProducerSnapshot> MediasoupSfuEngine::collectProducerSnapshotsNoLock(std::string_view roomId) const {
        std::vector<MediaProducerSnapshot> result;
        for (const auto& [producerId, producer] : producers_) {
            if (producer.roomId != roomId) {
                continue;
            }
            result.push_back(MediaProducerSnapshot{ producerId, producer.peerId, producer.kind, producer.trackType });
        }
        return result;
    }

    void MediasoupSfuEngine::clearPeerRuntimeNoLock(
        std::string_view roomId,
        std::string_view peerId,
        const std::unordered_set<std::string>& closingProducerIds) {
        for (auto transportIt = transports_.begin(); transportIt != transports_.end();) {
            if (transportIt->second.roomId == roomId && transportIt->second.peerId == peerId) {
                transportIt = transports_.erase(transportIt);
                continue;
            }
            ++transportIt;
        }

        for (auto producerIt = producers_.begin(); producerIt != producers_.end();) {
            const bool samePeer = producerIt->second.roomId == roomId && producerIt->second.peerId == peerId;
            if (samePeer) {
                producerIt = producers_.erase(producerIt);
                continue;
            }
            ++producerIt;
        }

        for (auto consumerIt = consumers_.begin(); consumerIt != consumers_.end();) {
            const bool sameConsumerPeer = consumerIt->second.roomId == roomId && consumerIt->second.peerId == peerId;
            const bool sameProducer = closingProducerIds.find(consumerIt->second.producerId) != closingProducerIds.end();
            if (sameConsumerPeer || sameProducer) {
                consumerIt = consumers_.erase(consumerIt);
                continue;
            }
            ++consumerIt;
        }

        signalingByPeer_.erase(std::string(peerId));
        peers_.erase(std::string(peerId));
    }

    core::contracts::OperationStatus MediasoupSfuEngine::leaveSessionNoLock(
        const MediaTransportCommand& command,
        std::vector<MediaTransportEvent>& emittedEvents,
        std::string reason,
        std::string backendOperation) {
        const auto roomValidation = requireField(!command.roomId.empty(), "roomId");
        if (!roomValidation.ok) {
            emittedEvents.push_back(makeErrorEvent(command, roomValidation.message));
            return roomValidation;
        }
        const auto peerValidation = requireField(!command.peerId.empty(), "peerId");
        if (!peerValidation.ok) {
            emittedEvents.push_back(makeErrorEvent(command, peerValidation.message));
            return peerValidation;
        }

        auto roomIt = rooms_.find(command.roomId);
        if (roomIt == rooms_.end()) {
            const auto failure = core::contracts::OperationStatus::failure("Room not found: " + command.roomId);
            emittedEvents.push_back(makeErrorEvent(command, failure.message));
            return failure;
        }
        if (roomIt->second.peers.find(command.peerId) == roomIt->second.peers.end()) {
            const auto failure = core::contracts::OperationStatus::failure("Peer is not joined to room.");
            emittedEvents.push_back(makeErrorEvent(command, failure.message));
            return failure;
        }

        std::vector<MediaProducerSnapshot> closingProducers;
        std::unordered_set<std::string> closingProducerIds;
        for (const auto& [producerId, producer] : producers_) {
            if (producer.roomId == command.roomId && producer.peerId == command.peerId) {
                closingProducers.push_back(
                    MediaProducerSnapshot{ producerId, producer.peerId, producer.kind, producer.trackType });
                closingProducerIds.insert(producerId);
            }
        }

        std::string backendMessage;
        const auto backendStatus = callMediasoupBackendNoLock(backendOperation, command, backendMessage);
        if (!backendStatus.ok) {
            emittedEvents.push_back(makeErrorEvent(command, backendStatus.message));
            return backendStatus;
        }

        roomIt->second.peers.erase(command.peerId);
        const auto remainingPeers = collectRoomPeersNoLock(command.roomId);
        clearPeerRuntimeNoLock(command.roomId, command.peerId, closingProducerIds);

        for (const auto& producer : closingProducers) {
            MediaTransportEvent event;
            event.type = MediaTransportEventType::TrackClosed;
            event.correlationId = command.correlationId;
            event.peerId = command.peerId;
            event.roomId = command.roomId;
            event.producerId = producer.producerId;
            event.kind = producer.kind;
            event.trackType = producer.trackType;
            event.notifyPeerIds = remainingPeers;
            emittedEvents.push_back(std::move(event));
        }

        MediaTransportEvent peerLeft;
        peerLeft.type = MediaTransportEventType::PeerLeft;
        peerLeft.correlationId = command.correlationId;
        peerLeft.peerId = command.peerId;
        peerLeft.roomId = command.roomId;
        peerLeft.reason = reason;
        peerLeft.memberPeerIds = remainingPeers;
        peerLeft.notifyPeerIds = remainingPeers;
        emittedEvents.push_back(std::move(peerLeft));

        MediaTransportEvent sessionClosed;
        sessionClosed.type = MediaTransportEventType::SessionClosed;
        sessionClosed.correlationId = command.correlationId;
        sessionClosed.peerId = command.peerId;
        sessionClosed.roomId = command.roomId;
        sessionClosed.reason = reason;
        sessionClosed.notifyPeerIds.push_back(command.peerId);
        emittedEvents.push_back(std::move(sessionClosed));

        if (roomIt->second.peers.empty()) {
            MediaTransportEvent lifecycle;
            lifecycle.type = MediaTransportEventType::SessionEnded;
            lifecycle.correlationId = command.correlationId;
            lifecycle.peerId = command.peerId;
            lifecycle.roomId = command.roomId;
            lifecycle.ended = true;
            lifecycle.reason = "room_empty";
            lifecycle.notifyPeerIds.push_back(command.peerId);
            emittedEvents.push_back(std::move(lifecycle));
            rooms_.erase(roomIt);
        }

        return { true, std::move(backendMessage) };
    }

    core::contracts::OperationStatus MediasoupSfuEngine::executeIntentNoLock(
        MediaTransportIntent intent,
        const MediaTransportCommand& command,
        std::vector<MediaTransportEvent>& emittedEvents) {
        emittedEvents.clear();
        const auto operationName = resolveOperationName(intent);
        if (operationName == "unknown") {
            const auto failure = core::contracts::OperationStatus::failure("Unsupported media intent.");
            emittedEvents.push_back(makeErrorEvent(command, failure.message));
            return failure;
        }
        const auto policyStatus = mediaPolicy_.validateAndConsume(intent, command);
        if (!policyStatus.ok) {
            emittedEvents.push_back(makeErrorEvent(command, policyStatus.message));
            return policyStatus;
        }

        switch (intent) {
        case MediaTransportIntent::CreateRoom: {
            const auto roomValidation = requireField(!command.roomId.empty(), "roomId");
            if (!roomValidation.ok) {
                emittedEvents.push_back(makeErrorEvent(command, roomValidation.message));
                return roomValidation;
            }
            if (rooms_.find(command.roomId) != rooms_.end()) {
                const auto failure = core::contracts::OperationStatus::failure("Room already exists: " + command.roomId);
                emittedEvents.push_back(makeErrorEvent(command, failure.message));
                return failure;
            }

            std::string backendMessage;
            const auto backendStatus = callMediasoupBackendNoLock(operationName, command, backendMessage);
            if (!backendStatus.ok) {
                if (containsCaseInsensitive(backendStatus.message, "already exists")) {
                    rooms_.emplace(command.roomId, RoomState{});
                    return core::contracts::OperationStatus::success(
                        "Media room already existed in backend; local state synchronized.");
                }
                emittedEvents.push_back(makeErrorEvent(command, backendStatus.message));
                return backendStatus;
            }

            rooms_.emplace(command.roomId, RoomState{});
            return { true, std::move(backendMessage) };
        }
        case MediaTransportIntent::JoinSession: {
            const auto roomValidation = requireField(!command.roomId.empty(), "roomId");
            if (!roomValidation.ok) {
                emittedEvents.push_back(makeErrorEvent(command, roomValidation.message));
                return roomValidation;
            }
            const auto peerValidation = requireField(!command.peerId.empty(), "peerId");
            if (!peerValidation.ok) {
                emittedEvents.push_back(makeErrorEvent(command, peerValidation.message));
                return peerValidation;
            }

            auto roomIt = rooms_.find(command.roomId);
            if (roomIt == rooms_.end()) {
                const auto failure = core::contracts::OperationStatus::failure("Room not found: " + command.roomId);
                emittedEvents.push_back(makeErrorEvent(command, failure.message));
                return failure;
            }

            std::string backendMessage;
            const auto backendStatus = callMediasoupBackendNoLock(operationName, command, backendMessage);
            if (!backendStatus.ok) {
                emittedEvents.push_back(makeErrorEvent(command, backendStatus.message));
                return backendStatus;
            }

            const auto previousPeers = collectRoomPeersNoLock(command.roomId);
            const auto before = roomIt->second.peers.size();
            roomIt->second.peers.insert(command.peerId);
            peers_[command.peerId] = PeerState{ command.roomId, command.sessionHandle };
            const auto currentPeers = collectRoomPeersNoLock(command.roomId);

            MediaTransportEvent roomState;
            roomState.type = MediaTransportEventType::RoomState;
            roomState.correlationId = command.correlationId;
            roomState.peerId = command.peerId;
            roomState.roomId = command.roomId;
            roomState.memberPeerIds = currentPeers;
            roomState.activeProducers = collectProducerSnapshotsNoLock(command.roomId);
            roomState.notifyPeerIds.push_back(command.peerId);
            emittedEvents.push_back(std::move(roomState));

            MediaTransportEvent peerJoined;
            peerJoined.type = MediaTransportEventType::PeerJoined;
            peerJoined.correlationId = command.correlationId;
            peerJoined.peerId = command.peerId;
            peerJoined.roomId = command.roomId;
            peerJoined.memberPeerIds = currentPeers;
            peerJoined.notifyPeerIds = previousPeers;
            emittedEvents.push_back(std::move(peerJoined));

            if (before == 0 && !roomIt->second.peers.empty()) {
                MediaTransportEvent lifecycle;
                lifecycle.type = MediaTransportEventType::SessionStarted;
                lifecycle.correlationId = command.correlationId;
                lifecycle.peerId = command.peerId;
                lifecycle.roomId = command.roomId;
                lifecycle.started = true;
                lifecycle.reason = "joined";
                lifecycle.memberPeerIds = currentPeers;
                lifecycle.notifyPeerIds = currentPeers;
                emittedEvents.push_back(std::move(lifecycle));
            }

            return { true, std::move(backendMessage) };
        }
        case MediaTransportIntent::LeaveSession:
            return leaveSessionNoLock(command, emittedEvents, "left", operationName);
        case MediaTransportIntent::OpenTransport: {
            const auto roomValidation = requireField(!command.roomId.empty(), "roomId");
            if (!roomValidation.ok) {
                emittedEvents.push_back(makeErrorEvent(command, roomValidation.message));
                return roomValidation;
            }
            const auto peerValidation = requireField(!command.peerId.empty(), "peerId");
            if (!peerValidation.ok) {
                emittedEvents.push_back(makeErrorEvent(command, peerValidation.message));
                return peerValidation;
            }
            const auto transportValidation = requireField(!command.transportId.empty(), "transportId");
            if (!transportValidation.ok) {
                emittedEvents.push_back(makeErrorEvent(command, transportValidation.message));
                return transportValidation;
            }
            if (transports_.find(command.transportId) != transports_.end()) {
                const auto failure = core::contracts::OperationStatus::failure("Transport already exists: " + command.transportId);
                emittedEvents.push_back(makeErrorEvent(command, failure.message));
                return failure;
            }
            auto roomIt = rooms_.find(command.roomId);
            if (roomIt == rooms_.end() || roomIt->second.peers.find(command.peerId) == roomIt->second.peers.end()) {
                const auto failure = core::contracts::OperationStatus::failure("Peer must join room before opening transport.");
                emittedEvents.push_back(makeErrorEvent(command, failure.message));
                return failure;
            }

            std::string backendMessage;
            const auto backendStatus = callMediasoupBackendNoLock(operationName, command, backendMessage);
            if (!backendStatus.ok) {
                emittedEvents.push_back(makeErrorEvent(command, backendStatus.message));
                return backendStatus;
            }

            transports_[command.transportId] = TransportState{ command.roomId, command.peerId };
            MediaTransportEvent event;
            event.type = MediaTransportEventType::TransportOpened;
            event.correlationId = command.correlationId;
            event.peerId = command.peerId;
            event.roomId = command.roomId;
            event.transportId = command.transportId;
            event.reason = "transport_opened";
            event.notifyPeerIds.push_back(command.peerId);
            emittedEvents.push_back(std::move(event));
            return { true, std::move(backendMessage) };
        }
        case MediaTransportIntent::PublishTrack: {
            const auto producerValidation = requireField(!command.producerId.empty(), "producerId");
            if (!producerValidation.ok) {
                emittedEvents.push_back(makeErrorEvent(command, producerValidation.message));
                return producerValidation;
            }
            const auto transportIt = transports_.find(command.transportId);
            if (transportIt == transports_.end()) {
                const auto failure = core::contracts::OperationStatus::failure("Transport not found: " + command.transportId);
                emittedEvents.push_back(makeErrorEvent(command, failure.message));
                return failure;
            }
            if (producers_.find(command.producerId) != producers_.end()) {
                const auto failure = core::contracts::OperationStatus::failure("Producer already exists: " + command.producerId);
                emittedEvents.push_back(makeErrorEvent(command, failure.message));
                return failure;
            }

            std::string backendMessage;
            const auto backendStatus = callMediasoupBackendNoLock(operationName, command, backendMessage);
            if (!backendStatus.ok) {
                emittedEvents.push_back(makeErrorEvent(command, backendStatus.message));
                return backendStatus;
            }

            const auto response = parseBackendResponse(backendMessage);
            const auto data = response.contains("data") && response["data"].is_object()
                ? response["data"]
                : json::object();
            const auto resolvedKind = data.value("kind", command.kind);
            const auto resolvedTrackType = data.value("trackType", command.trackType);

            producers_[command.producerId] = ProducerState{
                transportIt->second.roomId,
                transportIt->second.peerId,
                command.transportId,
                resolvedKind,
                resolvedTrackType
            };

            std::vector<std::string> notifyPeers;
            const auto roomPeers = collectRoomPeersNoLock(transportIt->second.roomId);
            for (const auto& roomPeer : roomPeers) {
                if (roomPeer != command.peerId) {
                    notifyPeers.push_back(roomPeer);
                }
            }

            MediaTransportEvent event;
            event.type = MediaTransportEventType::TrackPublished;
            event.correlationId = command.correlationId;
            event.peerId = command.peerId;
            event.roomId = command.roomId;
            event.transportId = command.transportId;
            event.producerId = command.producerId;
            event.kind = resolvedKind;
            event.trackType = resolvedTrackType;
            event.memberPeerIds = roomPeers;
            event.notifyPeerIds = std::move(notifyPeers);
            emittedEvents.push_back(std::move(event));
            return { true, std::move(backendMessage) };
        }
        case MediaTransportIntent::PauseTrack:
        case MediaTransportIntent::ResumeTrack:
        case MediaTransportIntent::CloseTrack: {
            const auto peerValidation = requireField(!command.peerId.empty(), "peerId");
            if (!peerValidation.ok) {
                emittedEvents.push_back(makeErrorEvent(command, peerValidation.message));
                return peerValidation;
            }
            const auto producerValidation = requireField(!command.producerId.empty(), "producerId");
            if (!producerValidation.ok) {
                emittedEvents.push_back(makeErrorEvent(command, producerValidation.message));
                return producerValidation;
            }

            auto producerIt = producers_.find(command.producerId);
            if (producerIt == producers_.end()) {
                const auto failure = core::contracts::OperationStatus::failure("Producer not found: " + command.producerId);
                emittedEvents.push_back(makeErrorEvent(command, failure.message));
                return failure;
            }
            if (producerIt->second.peerId != command.peerId) {
                const auto failure = core::contracts::OperationStatus::failure("Producer is not owned by peer.");
                emittedEvents.push_back(makeErrorEvent(command, failure.message));
                return failure;
            }
            if (!command.roomId.empty() && producerIt->second.roomId != command.roomId) {
                const auto failure = core::contracts::OperationStatus::failure("Producer is registered in another room.");
                emittedEvents.push_back(makeErrorEvent(command, failure.message));
                return failure;
            }

            MediaTransportCommand backendCommand = command;
            backendCommand.roomId = producerIt->second.roomId;

            std::string backendMessage;
            const auto backendStatus = callMediasoupBackendNoLock(operationName, backendCommand, backendMessage);
            if (!backendStatus.ok) {
                emittedEvents.push_back(makeErrorEvent(command, backendStatus.message));
                return backendStatus;
            }

            const auto roomPeers = collectRoomPeersNoLock(producerIt->second.roomId);
            std::vector<std::string> notifyPeers;
            notifyPeers.reserve(roomPeers.size());
            for (const auto& roomPeer : roomPeers) {
                if (roomPeer != command.peerId) {
                    notifyPeers.push_back(roomPeer);
                }
            }

            if (intent == MediaTransportIntent::CloseTrack) {
                const auto producerRoomId = producerIt->second.roomId;
                const auto producerKind = producerIt->second.kind;
                const auto producerTrackType = producerIt->second.trackType;
                producers_.erase(producerIt);

                for (auto consumerIt = consumers_.begin(); consumerIt != consumers_.end();) {
                    if (consumerIt->second.producerId == command.producerId
                        && consumerIt->second.roomId == producerRoomId) {
                        consumerIt = consumers_.erase(consumerIt);
                        continue;
                    }
                    ++consumerIt;
                }

                MediaTransportEvent event;
                event.type = MediaTransportEventType::TrackClosed;
                event.correlationId = command.correlationId;
                event.peerId = command.peerId;
                event.roomId = producerRoomId;
                event.producerId = command.producerId;
                event.kind = producerKind;
                event.trackType = producerTrackType;
                event.reason = "closed";
                event.memberPeerIds = roomPeers;
                event.notifyPeerIds = std::move(notifyPeers);
                emittedEvents.push_back(std::move(event));
                return { true, std::move(backendMessage) };
            }

            MediaTransportEvent event;
            event.type = intent == MediaTransportIntent::PauseTrack
                ? MediaTransportEventType::TrackClosed
                : MediaTransportEventType::TrackPublished;
            event.correlationId = command.correlationId;
            event.peerId = command.peerId;
            event.roomId = producerIt->second.roomId;
            event.producerId = command.producerId;
            event.kind = producerIt->second.kind;
            event.trackType = producerIt->second.trackType;
            event.reason = intent == MediaTransportIntent::PauseTrack ? "paused" : "resumed";
            event.memberPeerIds = roomPeers;
            event.notifyPeerIds = std::move(notifyPeers);
            emittedEvents.push_back(std::move(event));

            return { true, std::move(backendMessage) };
        }
        case MediaTransportIntent::ConsumeTrack: {
            const auto roomValidation = requireField(!command.roomId.empty(), "roomId");
            if (!roomValidation.ok) {
                emittedEvents.push_back(makeErrorEvent(command, roomValidation.message));
                return roomValidation;
            }
            const auto peerValidation = requireField(!command.peerId.empty(), "peerId");
            if (!peerValidation.ok) {
                emittedEvents.push_back(makeErrorEvent(command, peerValidation.message));
                return peerValidation;
            }
            const auto transportValidation = requireField(!command.transportId.empty(), "transportId");
            if (!transportValidation.ok) {
                emittedEvents.push_back(makeErrorEvent(command, transportValidation.message));
                return transportValidation;
            }
            const auto transportIt = transports_.find(command.transportId);
            if (transportIt == transports_.end()) {
                const auto failure = core::contracts::OperationStatus::failure("Transport not found: " + command.transportId);
                emittedEvents.push_back(makeErrorEvent(command, failure.message));
                return failure;
            }
            if (transportIt->second.roomId != command.roomId) {
                const auto failure = core::contracts::OperationStatus::failure("Transport is registered in another room.");
                emittedEvents.push_back(makeErrorEvent(command, failure.message));
                return failure;
            }
            if (transportIt->second.peerId != command.peerId) {
                const auto failure = core::contracts::OperationStatus::failure("Transport is not owned by peer.");
                emittedEvents.push_back(makeErrorEvent(command, failure.message));
                return failure;
            }
            const auto producerIt = producers_.find(command.producerId);
            if (producerIt == producers_.end()) {
                const auto failure = core::contracts::OperationStatus::failure("Producer not found: " + command.producerId);
                emittedEvents.push_back(makeErrorEvent(command, failure.message));
                return failure;
            }
            if (producerIt->second.roomId != command.roomId) {
                const auto failure = core::contracts::OperationStatus::failure("Producer is registered in another room.");
                emittedEvents.push_back(makeErrorEvent(command, failure.message));
                return failure;
            }

            std::string backendMessage;
            const auto backendStatus = callMediasoupBackendNoLock(operationName, command, backendMessage);
            if (!backendStatus.ok) {
                emittedEvents.push_back(makeErrorEvent(command, backendStatus.message));
                return backendStatus;
            }

            const auto response = parseBackendResponse(backendMessage);
            const auto data = response.contains("data") && response["data"].is_object()
                ? response["data"]
                : json::object();
            const auto consumerId = data.value("consumerId", command.consumerId);
            const auto kind = data.value("kind", producerIt->second.kind);
            const auto trackType = data.value("trackType", producerIt->second.trackType);
            const auto paused = data.value("paused", true);

            if (!consumerId.empty()) {
                consumers_[consumerId] = ConsumerState{
                    command.roomId,
                    command.peerId,
                    command.transportId,
                    command.producerId,
                    producerIt->second.peerId,
                    kind,
                    trackType,
                    paused
                };
            }

            return { true, std::move(backendMessage) };
        }
        case MediaTransportIntent::ResumeConsumer: {
            const auto peerValidation = requireField(!command.peerId.empty(), "peerId");
            if (!peerValidation.ok) {
                emittedEvents.push_back(makeErrorEvent(command, peerValidation.message));
                return peerValidation;
            }
            const auto consumerValidation = requireField(!command.consumerId.empty(), "consumerId");
            if (!consumerValidation.ok) {
                emittedEvents.push_back(makeErrorEvent(command, consumerValidation.message));
                return consumerValidation;
            }

            auto consumerIt = consumers_.find(command.consumerId);
            if (consumerIt == consumers_.end()) {
                const auto failure = core::contracts::OperationStatus::failure("Consumer not found: " + command.consumerId);
                emittedEvents.push_back(makeErrorEvent(command, failure.message));
                return failure;
            }
            if (consumerIt->second.peerId != command.peerId) {
                const auto failure = core::contracts::OperationStatus::failure("Consumer is not owned by peer.");
                emittedEvents.push_back(makeErrorEvent(command, failure.message));
                return failure;
            }

            MediaTransportCommand backendCommand = command;
            backendCommand.roomId = consumerIt->second.roomId;
            std::string backendMessage;
            const auto backendStatus = callMediasoupBackendNoLock(operationName, backendCommand, backendMessage);
            if (!backendStatus.ok) {
                emittedEvents.push_back(makeErrorEvent(command, backendStatus.message));
                return backendStatus;
            }

            consumerIt->second.paused = false;
            MediaTransportEvent event;
            event.type = MediaTransportEventType::ConsumerResumed;
            event.correlationId = command.correlationId;
            event.peerId = command.peerId;
            event.roomId = consumerIt->second.roomId;
            event.consumerId = command.consumerId;
            event.producerId = consumerIt->second.producerId;
            event.producerPeerId = consumerIt->second.producerPeerId;
            event.kind = consumerIt->second.kind;
            event.trackType = consumerIt->second.trackType;
            event.paused = false;
            event.notifyPeerIds.push_back(command.peerId);
            emittedEvents.push_back(std::move(event));
            return { true, std::move(backendMessage) };
        }
        case MediaTransportIntent::ApplyOffer:
        case MediaTransportIntent::ApplyIce: {
            const auto peerValidation = requireField(!command.peerId.empty(), "peerId");
            if (!peerValidation.ok) {
                emittedEvents.push_back(makeErrorEvent(command, peerValidation.message));
                return peerValidation;
            }
            const auto transportValidation = requireField(!command.transportId.empty(), "transportId");
            if (!transportValidation.ok) {
                emittedEvents.push_back(makeErrorEvent(command, transportValidation.message));
                return transportValidation;
            }

            const auto transportIt = transports_.find(command.transportId);
            if (transportIt == transports_.end()) {
                const auto failure =
                    core::contracts::OperationStatus::failure("Transport not found: " + command.transportId);
                emittedEvents.push_back(makeErrorEvent(command, failure.message));
                return failure;
            }
            if (!command.roomId.empty() && transportIt->second.roomId != command.roomId) {
                const auto failure =
                    core::contracts::OperationStatus::failure("Transport is registered in another room.");
                emittedEvents.push_back(makeErrorEvent(command, failure.message));
                return failure;
            }
            if (transportIt->second.peerId != command.peerId) {
                const auto failure =
                    core::contracts::OperationStatus::failure("Transport is not owned by peer.");
                emittedEvents.push_back(makeErrorEvent(command, failure.message));
                return failure;
            }

            if (intent == MediaTransportIntent::ApplyIce) {
                nlohmann::json response = {
                    { "ok", true },
                    { "message", "ICE candidate accepted locally (mediasoup ICE Lite / noop backend path)." },
                    { "data", {
                        { "transportId", command.transportId },
                        { "accepted", true },
                        { "mode", "noop" }
                    } }
                };
                return { true, response.dump() };
            }

            MediaTransportCommand backendCommand = command;
            if (backendCommand.roomId.empty()) {
                backendCommand.roomId = transportIt->second.roomId;
            }

            std::string backendMessage;
            const auto backendStatus =
                callMediasoupBackendNoLock(resolveOperationName(intent), backendCommand, backendMessage);
            if (!backendStatus.ok) {
                emittedEvents.push_back(makeErrorEvent(backendCommand, backendStatus.message));
                return backendStatus;
            }

            return { true, std::move(backendMessage) };
        }
        case MediaTransportIntent::ReadStats: {
            std::string backendMessage;
            const auto backendStatus = callMediasoupBackendNoLock(operationName, command, backendMessage);
            if (!backendStatus.ok) {
                emittedEvents.push_back(makeErrorEvent(command, backendStatus.message));
                return backendStatus;
            }
            return { true, std::move(backendMessage) };
        }
        case MediaTransportIntent::CloseSession: {
            if (command.peerId.empty()) {
                const auto failure = core::contracts::OperationStatus::failure("peerId must not be empty.");
                emittedEvents.push_back(makeErrorEvent(command, failure.message));
                return failure;
            }

            auto peerIt = peers_.find(command.peerId);
            if (peerIt == peers_.end()) {
                nlohmann::json payload = {
                    { "ok", true },
                    { "message", "Peer session already closed." },
                    { "data", {
                        { "peerId", command.peerId },
                        { "roomId", command.roomId },
                        { "alreadyClosed", true }
                    } }
                };
                return { true, payload.dump() };
            }

            MediaTransportCommand closeCommand = command;
            if (closeCommand.roomId.empty()) {
                closeCommand.roomId = peerIt->second.roomId;
            }
            return leaveSessionNoLock(closeCommand, emittedEvents, "webrtc_close", operationName);
        }
        }

        const auto failure = core::contracts::OperationStatus::failure("Unsupported media intent.");
        emittedEvents.push_back(makeErrorEvent(command, failure.message));
        return failure;
    }
} // namespace eds::server_new::mediasoup::service