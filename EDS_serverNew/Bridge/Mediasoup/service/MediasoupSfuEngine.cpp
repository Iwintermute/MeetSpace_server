#include "Bridge/Mediasoup/service/MediasoupSfuEngine.h"

#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/system/system_error.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
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


}

MediasoupSfuEngine::MediasoupSfuEngine(bool debugMode)
    : debugMode_(debugMode),
      backendUrl_(defaultBackendUrl()) {
    if (!parseBackendUrl(backendUrl_, backendHost_, backendPort_, backendPath_)) {
        backendHost_.clear();
        backendPort_.clear();
        backendPath_.clear();
        if (debugMode_) {
            std::cout << "[mediasoup][verify] backend url is not configured or invalid. "
                      << "Set EDUSPACE_MEDIASOUP_BACKEND_URL.\n";
        }
    }

    resolver_ = std::make_unique<tcp::resolver>(ioContext_);
    socket_ = std::make_unique<ws_stream>(ioContext_);
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
    static_cast<void>(leaveSessionNoLock(
        command,
        emittedEvents,
        "disconnected",
        "router.closePeer"));
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
    event.reason = std::move(reason);
    return event;
}

bool MediasoupSfuEngine::parseBackendUrl(
    std::string_view url,
    std::string& host,
    std::string& port,
    std::string& path) {
    constexpr std::string_view wsPrefix = "ws://";
    if (url.rfind(wsPrefix, 0) != 0) {
        return false;
    }

    auto remaining = url.substr(wsPrefix.size());
    const auto slashPos = remaining.find('/');
    const auto hostPort = slashPos == std::string_view::npos
        ? remaining
        : remaining.substr(0, slashPos);
    path = slashPos == std::string_view::npos
        ? "/"
        : std::string(remaining.substr(slashPos));

    const auto colonPos = hostPort.rfind(':');
    if (colonPos == std::string_view::npos) {
        host = std::string(hostPort);
        port = "80";
    } else {
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

std::string MediasoupSfuEngine::defaultBackendUrl() {
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
    case MediaTransportIntent::ConsumeTrack:
        return "transport.consume";
    case MediaTransportIntent::ApplyOffer:
        return "transport.connectDtls";
    case MediaTransportIntent::ApplyIce:
        return "transport.addIceCandidate";
    case MediaTransportIntent::CloseSession:
        return "router.closePeer";
    }
    return "unknown";
}
bool MediasoupSfuEngine::isMediasoupEngineName(std::string_view value) {
    if (value.empty()) {
        return false;
    }
    const auto normalized = toLowerCopy(value);
    return normalized.find("mediasoup") != std::string::npos;
}

core::contracts::OperationStatus MediasoupSfuEngine::ensureConnectedNoLock() {
    if (backendConnected_ && backendVerified_) {
        return core::contracts::OperationStatus::success();
    }
    if (backendConnected_ && !backendVerified_) {
        return verifyMediasoupBackendNoLock();
    }
    if (backendHost_.empty() || backendPort_.empty()) {
        return core::contracts::OperationStatus::failure(
            "Mediasoup backend endpoint is not configured. Set EDUSPACE_MEDIASOUP_BACKEND_URL.");
    }

    try {
        if (!resolver_) {
            resolver_ = std::make_unique<tcp::resolver>(ioContext_);
        }
        if (!socket_) {
            socket_ = std::make_unique<ws_stream>(ioContext_);
        }

        const auto endpoints = resolver_->resolve(backendHost_, backendPort_);
        boost::beast::get_lowest_layer(*socket_).connect(endpoints);
        socket_->handshake(backendHost_ + ":" + backendPort_, backendPath_);
        backendConnected_ = true;

        const auto verificationStatus = verifyMediasoupBackendNoLock();
        if (!verificationStatus.ok) {
            disconnectNoLock();
            return verificationStatus;
        }
        return core::contracts::OperationStatus::success();
    } catch (const boost::system::system_error& error) {
        disconnectNoLock();
        return core::contracts::OperationStatus::failure(
            std::string("Failed to connect mediasoup backend: ") + error.what());
    }
}
core::contracts::OperationStatus MediasoupSfuEngine::verifyMediasoupBackendNoLock() {
    if (!backendConnected_) {
        return core::contracts::OperationStatus::failure("Mediasoup backend websocket is not connected.");
    }
    if (!socket_) {
        return core::contracts::OperationStatus::failure("Mediasoup backend socket is not initialized.");
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
        const auto serialized = request.dump();
        socket_->write(boost::asio::buffer(serialized));

        boost::beast::flat_buffer responseBuffer;
        socket_->read(responseBuffer);
        const auto responseText = boost::beast::buffers_to_string(responseBuffer.data());
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
        if (debugMode_) {
            std::cout << "[mediasoup][verify] backend confirmed"
                      << " engine=" << backendEngine_
                      << " version=" << (backendVersion_.empty() ? "unknown" : backendVersion_)
                      << "\n";
        }
        return core::contracts::OperationStatus::success();
    } catch (const std::exception& error) {
        return core::contracts::OperationStatus::failure(
            std::string("Mediasoup backend verification failed: ") + error.what());
    }
}

void MediasoupSfuEngine::disconnectNoLock() {
    if (!socket_) {
        backendConnected_ = false;
        backendVerified_ = false;
        backendEngine_.clear();
        backendVersion_.clear();
        return;
    }

    boost::system::error_code code;
    socket_->close(boost::beast::websocket::close_code::normal, code);
    socket_.reset();
    socket_ = std::make_unique<ws_stream>(ioContext_);
    backendConnected_ = false;
    backendVerified_ = false;
    backendEngine_.clear();
    backendVersion_.clear();
}

core::contracts::OperationStatus MediasoupSfuEngine::callMediasoupBackendNoLock(
    const std::string& operationName,
    const MediaTransportCommand& command,
    std::string& backendMessage) {
    const auto connectionStatus = ensureConnectedNoLock();
    if (!connectionStatus.ok) {
        return connectionStatus;
    }
    if (!backendVerified_) {
        return core::contracts::OperationStatus::failure(
            "Mediasoup backend is connected but not verified.");
    }
    if (!socket_) {
        return core::contracts::OperationStatus::failure("Mediasoup backend socket is not initialized.");
    }

    try {
        json request{
            { "id", makeRequestId(command.correlationId) },
            { "operation", operationName },
            { "requireEngine", "mediasoup" },
            { "payload", {
                { "sessionHandle", command.sessionHandle },
                { "sessionId", command.sessionId },
                { "peerId", command.peerId },
                { "roomId", command.roomId },
                { "transportId", command.transportId },
                { "producerId", command.producerId },
                { "kind", command.kind },
                { "sdp", command.sdp },
                { "sdpMid", command.sdpMid },
                { "candidate", command.candidate },
                { "correlationId", command.correlationId }
            } }
        };

        const auto serialized = request.dump();
        socket_->write(boost::asio::buffer(serialized));

        boost::beast::flat_buffer responseBuffer;
        socket_->read(responseBuffer);
        backendMessage = boost::beast::buffers_to_string(responseBuffer.data());
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
        return { true, message };
    } catch (const std::exception& error) {
        disconnectNoLock();
        return core::contracts::OperationStatus::failure(
            std::string("Mediasoup backend request failed: ") + error.what());
    }
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

void MediasoupSfuEngine::clearPeerRuntimeNoLock(std::string_view roomId, std::string_view peerId) {
    std::unordered_set<std::string> transportIds;
    for (auto transportIt = transports_.begin(); transportIt != transports_.end();) {
        if (transportIt->second.roomId == roomId && transportIt->second.peerId == peerId) {
            transportIds.insert(transportIt->first);
            transportIt = transports_.erase(transportIt);
            continue;
        }
        ++transportIt;
    }

    for (auto producerIt = producers_.begin(); producerIt != producers_.end();) {
        const bool samePeer = producerIt->second.roomId == roomId && producerIt->second.peerId == peerId;
        const bool removedTransport = transportIds.find(producerIt->second.transportId) != transportIds.end();
        if (samePeer || removedTransport) {
            producerIt = producers_.erase(producerIt);
            continue;
        }
        ++producerIt;
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

    std::string backendMessage;
    const auto backendStatus = callMediasoupBackendNoLock(backendOperation, command, backendMessage);
    if (!backendStatus.ok) {
        emittedEvents.push_back(makeErrorEvent(command, backendStatus.message));
        return backendStatus;
    }

    roomIt->second.peers.erase(command.peerId);
    clearPeerRuntimeNoLock(command.roomId, command.peerId);

    MediaTransportEvent sessionClosed;
    sessionClosed.type = MediaTransportEventType::SessionClosed;
    sessionClosed.correlationId = command.correlationId;
    sessionClosed.peerId = command.peerId;
    sessionClosed.roomId = command.roomId;
    sessionClosed.reason = std::move(reason);
    emittedEvents.push_back(std::move(sessionClosed));

    if (roomIt->second.peers.empty()) {
        MediaTransportEvent lifecycle;
        lifecycle.type = MediaTransportEventType::SessionEnded;
        lifecycle.correlationId = command.correlationId;
        lifecycle.peerId = command.peerId;
        lifecycle.roomId = command.roomId;
        lifecycle.ended = true;
        lifecycle.reason = "room_empty";
        lifecycle.memberPeerIds = {};
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

        const auto before = roomIt->second.peers.size();
        roomIt->second.peers.insert(command.peerId);
        peers_[command.peerId] = PeerState{ command.roomId, command.sessionHandle };

        if (before == 0 && !roomIt->second.peers.empty()) {
            MediaTransportEvent lifecycle;
            lifecycle.type = MediaTransportEventType::SessionStarted;
            lifecycle.correlationId = command.correlationId;
            lifecycle.peerId = command.peerId;
            lifecycle.roomId = command.roomId;
            lifecycle.started = true;
            lifecycle.reason = "joined";
            lifecycle.memberPeerIds = collectRoomPeersNoLock(command.roomId);
            lifecycle.notifyPeerIds = lifecycle.memberPeerIds;
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

        producers_[command.producerId] = ProducerState{
            transportIt->second.roomId,
            transportIt->second.peerId,
            command.transportId,
            command.kind
        };
        MediaTransportEvent event;
        event.type = MediaTransportEventType::TrackPublished;
        event.correlationId = command.correlationId;
        event.peerId = command.peerId;
        event.roomId = command.roomId;
        event.transportId = command.transportId;
        event.producerId = command.producerId;
        event.kind = command.kind;
        emittedEvents.push_back(std::move(event));
        return { true, std::move(backendMessage) };
    }
    case MediaTransportIntent::ConsumeTrack: {
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

        MediaTransportEvent event;
        event.type = MediaTransportEventType::TrackConsumed;
        event.correlationId = command.correlationId;
        event.peerId = command.peerId;
        event.roomId = command.roomId;
        event.producerId = command.producerId;
        emittedEvents.push_back(std::move(event));
        return { true, std::move(backendMessage) };
    }
    case MediaTransportIntent::ApplyOffer:
    case MediaTransportIntent::ApplyIce: {
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
            const auto failure = core::contracts::OperationStatus::failure("Peer session not found.");
            emittedEvents.push_back(makeErrorEvent(command, failure.message));
            return failure;
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
