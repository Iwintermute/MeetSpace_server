#include "Bridge/Mediasoup/service/MediasoupTransportService.h"

#include "Bridge/Mediasoup/runtime/MediasoupCommand.h"

#include <chrono>
#include <iostream>
#include <sstream>
#include <utility>

namespace eds::server_new::mediasoup::service {
namespace {

std::string joinRecipients(const std::vector<std::string>& recipients) {
    std::ostringstream stream;
    for (std::size_t i = 0; i < recipients.size(); ++i) {
        if (i > 0) {
            stream << ",";
        }
        stream << recipients[i];
    }
    return stream.str();
}

}

MediasoupTransportService::MediasoupTransportService(
    std::shared_ptr<eds::server_new::mediasoup::MediasoupStateStore> stateStore,
    std::shared_ptr<eds::server_new::mediasoup::MediasoupRtcBridge> rtcBridge,
    bool debugMode)
    : stateStore_(std::move(stateStore)),
      rtcBridge_(std::move(rtcBridge)),
      debugMode_(debugMode) {
    if (!stateStore_) {
        stateStore_ = std::make_shared<eds::server_new::mediasoup::MediasoupStateStore>();
    }
    if (!rtcBridge_) {
        rtcBridge_ = std::make_shared<eds::server_new::mediasoup::MediasoupRtcBridge>();
    }

    if (rtcBridge_) {
        rtcBridge_->setOnPeerBinary([this](std::string_view sourcePeerId, const std::vector<uint8_t>& payload) {
            relayPeerBinaryPayload(sourcePeerId, payload);
        });
    }
    if (debugMode_) {
        std::cout << "[mediasoup][debug][audio] transport service tracing enabled.\n";
    }
}

MediasoupTransportService::~MediasoupTransportService() {
    if (rtcBridge_) {
        rtcBridge_->setOnPeerBinary(nullptr);
    }
}

core::contracts::OperationStatus MediasoupTransportService::execute(
    MediaTransportIntent intent,
    const MediaTransportCommand& command,
    std::vector<MediaTransportEvent>& emittedEvents) {
    emittedEvents.clear();
    const auto mediasoupCommand = toMediasoupCommand(command);

    core::contracts::OperationStatus status = core::contracts::OperationStatus::success();
    switch (intent) {
    case MediaTransportIntent::CreateRoom:
        status = stateStore_->createRoom(mediasoupCommand);
        break;
    case MediaTransportIntent::JoinSession:
        status = stateStore_->joinRoom(mediasoupCommand);
        break;
    case MediaTransportIntent::LeaveSession:
        status = stateStore_->leaveRoom(mediasoupCommand);
        break;
    case MediaTransportIntent::OpenTransport:
        status = stateStore_->openTransport(mediasoupCommand);
        if (status.ok) {
            MediaTransportEvent event;
            event.type = MediaTransportEventType::TransportOpened;
            event.correlationId = command.correlationId;
            event.peerId = command.peerId;
            event.roomId = command.roomId;
            event.transportId = command.transportId;
            event.reason = "transport_opened";
            emittedEvents.push_back(std::move(event));
        }
        break;
    case MediaTransportIntent::PublishTrack:
        status = stateStore_->startProducing(mediasoupCommand);
        if (status.ok) {
            MediaTransportEvent event;
            event.type = MediaTransportEventType::TrackPublished;
            event.correlationId = command.correlationId;
            event.peerId = command.peerId;
            event.roomId = command.roomId;
            event.transportId = command.transportId;
            event.producerId = command.producerId;
            event.kind = command.kind;
            emittedEvents.push_back(std::move(event));
        }
        break;
    case MediaTransportIntent::ConsumeTrack:
        status = stateStore_->consume(mediasoupCommand);
        if (status.ok) {
            MediaTransportEvent event;
            event.type = MediaTransportEventType::TrackConsumed;
            event.correlationId = command.correlationId;
            event.peerId = command.peerId;
            event.roomId = command.roomId;
            event.producerId = command.producerId;
            emittedEvents.push_back(std::move(event));
        }
        break;
    case MediaTransportIntent::ApplyOffer:
        status = rtcBridge_->handleOffer(mediasoupCommand);
        break;
    case MediaTransportIntent::ApplyIce:
        status = rtcBridge_->handleIce(mediasoupCommand);
        break;
    case MediaTransportIntent::CloseSession:
        status = rtcBridge_->handleClose(mediasoupCommand);
        if (status.ok) {
            MediaTransportEvent event;
            event.type = MediaTransportEventType::SessionClosed;
            event.correlationId = command.correlationId;
            event.peerId = command.peerId;
            event.roomId = command.roomId;
            event.reason = "webrtc_close";
            emittedEvents.push_back(std::move(event));
        }
        break;
    }

    logCommandResult(intent, command, status);
    if (!status.ok) {
        emittedEvents.push_back(makeErrorEvent(command, status.message));
        return status;
    }

    appendLifecycleEventsForPeer(command.peerId, command.correlationId, emittedEvents);
    return status;
}

std::vector<MediaSignalingEvent> MediasoupTransportService::consumeSignalingEventsForPeer(std::string_view peerId) {
    std::vector<MediaSignalingEvent> result;
    if (!rtcBridge_ || peerId.empty()) {
        return result;
    }

    const auto signalingEvents = rtcBridge_->consumeEventsForPeer(peerId);
    result.reserve(signalingEvents.size());
    for (const auto& event : signalingEvents) {
        MediaSignalingEvent signaling;
        signaling.type = event.type;
        signaling.peerId = event.peerId;
        signaling.sdp = event.sdp;
        signaling.sdpMid = event.sdpMid;
        signaling.candidate = event.candidate;
        result.push_back(std::move(signaling));
    }
    return result;
}

void MediasoupTransportService::onSessionDisconnected(
    std::string_view peerId,
    std::uintptr_t sessionHandle,
    std::vector<MediaTransportEvent>& emittedEvents) {
    emittedEvents.clear();

    if (!peerId.empty()) {
        std::vector<eds::server_new::mediasoup::MediasoupStateStore::SessionLifecycleNotification> notifications;
        stateStore_->disconnectPeer(peerId, notifications);
        for (const auto& notification : notifications) {
            MediaTransportEvent event;
            event.type = MediaTransportEventType::SessionEnded;
            event.peerId = notification.actorPeerId;
            event.roomId = notification.roomId;
            event.reason = notification.reason;
            event.started = notification.started;
            event.ended = notification.ended;
            event.memberPeerIds = notification.memberPeerIds;
            event.notifyPeerIds = notification.notifyPeerIds;
            emittedEvents.push_back(std::move(event));
        }
    }

    if (rtcBridge_) {
        rtcBridge_->onSessionDisconnected(peerId, sessionHandle);
    }
    if (debugMode_) {
        std::cout << "[mediasoup][debug][audio] session_disconnected peer="
                  << peerId
                  << " session="
                  << sessionHandle
                  << " emitted_events="
                  << emittedEvents.size()
                  << "\n";
    }
}

void MediasoupTransportService::relayPeerBinaryPayload(
    std::string_view sourcePeerId,
    const std::vector<uint8_t>& payload) {
    if (!stateStore_ || !rtcBridge_) {
        return;
    }
    if (sourcePeerId.empty() || payload.empty()) {
        return;
    }

    const auto recipients = stateStore_->listOtherPeersInSameRoom(sourcePeerId);
    const auto packetBytes = static_cast<std::uint64_t>(payload.size());
    const auto sourcePeer = std::string(sourcePeerId);
    {
        std::lock_guard<std::mutex> lock(relayStatsMutex_);
        auto& sourceStats = relayStats_.perPeer[sourcePeer];
        ++relayStats_.ingressPackets;
        relayStats_.ingressBytes += packetBytes;
        ++sourceStats.producedPackets;
        sourceStats.producedBytes += packetBytes;
        if (recipients.empty()) {
            ++relayStats_.droppedPackets;
            ++sourceStats.droppedPackets;
        }
    }

    if (debugMode_) {
        std::cout << "[mediasoup][debug][audio] relay_input source=" << sourcePeer
                  << " bytes=" << packetBytes
                  << " recipients=" << recipients.size();
        if (!recipients.empty()) {
            std::cout << " targets=[" << joinRecipients(recipients) << "]";
        } else {
            std::cout << " dropped=true";
        }
        std::cout << "\n";
    }

    if (recipients.empty()) {
        logAudioRelaySummaryIfNeeded();
        return;
    }

    for (const auto& targetPeerId : recipients) {
        const auto sendStatus = rtcBridge_->sendBinaryToPeer(targetPeerId, payload);
        {
            std::lock_guard<std::mutex> lock(relayStatsMutex_);
            auto& sourceStats = relayStats_.perPeer[sourcePeer];
            if (sendStatus.ok) {
                ++relayStats_.forwardedCopies;
                relayStats_.forwardedBytes += packetBytes;
                ++sourceStats.forwardedCopies;
                sourceStats.forwardedBytes += packetBytes;
                auto& targetStats = relayStats_.perPeer[targetPeerId];
                ++targetStats.receivedCopies;
                targetStats.receivedBytes += packetBytes;
            } else {
                ++relayStats_.failedDeliveries;
                ++sourceStats.failedDeliveries;
            }
        }

        if (debugMode_) {
            std::cout << "[mediasoup][debug][audio] relay_delivery source=" << sourcePeer
                      << " target=" << targetPeerId
                      << " bytes=" << packetBytes
                      << " ok=" << (sendStatus.ok ? "true" : "false");
            if (!sendStatus.ok) {
                std::cout << " reason=\"" << sendStatus.message << "\"";
            }
            std::cout << "\n";
        }
    }
    logAudioRelaySummaryIfNeeded();
}

eds::server_new::mediasoup::MediasoupCommand MediasoupTransportService::toMediasoupCommand(
    const MediaTransportCommand& command) const {
    eds::server_new::mediasoup::MediasoupCommand mediasoupCommand;
    mediasoupCommand.sessionHandle = command.sessionHandle;
    mediasoupCommand.sessionId = command.sessionId;
    mediasoupCommand.peerId = command.peerId;
    mediasoupCommand.roomId = command.roomId;
    mediasoupCommand.transportId = command.transportId;
    mediasoupCommand.producerId = command.producerId;
    mediasoupCommand.kind = command.kind;
    mediasoupCommand.sdp = command.sdp;
    mediasoupCommand.sdpMid = command.sdpMid;
    mediasoupCommand.candidate = command.candidate;
    return mediasoupCommand;
}

void MediasoupTransportService::appendLifecycleEventsForPeer(
    std::string_view actorPeerId,
    std::string_view correlationId,
    std::vector<MediaTransportEvent>& emittedEvents) {
    if (actorPeerId.empty()) {
        return;
    }

    const auto notifications = stateStore_->consumeLifecycleNotificationsForPeer(actorPeerId);
    for (const auto& notification : notifications) {
        MediaTransportEvent event;
        event.type = notification.started ? MediaTransportEventType::SessionStarted : MediaTransportEventType::SessionEnded;
        event.correlationId = std::string(correlationId);
        event.peerId = notification.actorPeerId;
        event.roomId = notification.roomId;
        event.reason = notification.reason;
        event.started = notification.started;
        event.ended = notification.ended;
        event.memberPeerIds = notification.memberPeerIds;
        event.notifyPeerIds = notification.notifyPeerIds;
        emittedEvents.push_back(std::move(event));
    }
}

MediaTransportEvent MediasoupTransportService::makeErrorEvent(
    const MediaTransportCommand& command,
    std::string reason) const {
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

void MediasoupTransportService::logCommandResult(
    MediaTransportIntent intent,
    const MediaTransportCommand& command,
    const core::contracts::OperationStatus& status) const {
    if (!debugMode_) {
        return;
    }
    std::cout << "[mediasoup][debug][control] intent=" << intentToString(intent)
              << " peer=" << command.peerId
              << " room=" << command.roomId
              << " transport=" << command.transportId
              << " producer=" << command.producerId
              << " ok=" << (status.ok ? "true" : "false")
              << " message=\"" << status.message << "\""
              << "\n";
}

void MediasoupTransportService::logAudioRelaySummaryIfNeeded() const {
    if (!debugMode_) {
        return;
    }

    AudioRelayStats snapshot;
    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(relayStatsMutex_);
        if (relayStats_.ingressPackets == 0) {
            return;
        }

        const auto timeSinceLastSummary = now - relayStats_.lastSummaryAt;
        const auto summaryByPeriod = timeSinceLastSummary >= std::chrono::seconds(5);
        const auto summaryByPacketCount = relayStats_.ingressPackets % 50 == 0;
        if (!summaryByPeriod && !summaryByPacketCount) {
            return;
        }

        relayStats_.lastSummaryAt = now;
        snapshot = relayStats_;
    }

    std::cout << "[mediasoup][debug][audio] summary"
              << " ingress_packets=" << snapshot.ingressPackets
              << " ingress_bytes=" << snapshot.ingressBytes
              << " forwarded_copies=" << snapshot.forwardedCopies
              << " forwarded_bytes=" << snapshot.forwardedBytes
              << " dropped_packets=" << snapshot.droppedPackets
              << " failed_deliveries=" << snapshot.failedDeliveries
              << " peers=" << snapshot.perPeer.size()
              << "\n";

    for (const auto& [peerId, peerStats] : snapshot.perPeer) {
        std::cout << "[mediasoup][debug][audio] peer_stats"
                  << " peer=" << peerId
                  << " produced_packets=" << peerStats.producedPackets
                  << " produced_bytes=" << peerStats.producedBytes
                  << " forwarded_copies=" << peerStats.forwardedCopies
                  << " forwarded_bytes=" << peerStats.forwardedBytes
                  << " received_copies=" << peerStats.receivedCopies
                  << " received_bytes=" << peerStats.receivedBytes
                  << " dropped_packets=" << peerStats.droppedPackets
                  << " failed_deliveries=" << peerStats.failedDeliveries
                  << "\n";
    }
}

std::string_view MediasoupTransportService::intentToString(MediaTransportIntent intent) noexcept {
    switch (intent) {
    case MediaTransportIntent::CreateRoom:
        return "create_room";
    case MediaTransportIntent::JoinSession:
        return "join_session";
    case MediaTransportIntent::LeaveSession:
        return "leave_session";
    case MediaTransportIntent::OpenTransport:
        return "open_transport";
    case MediaTransportIntent::PublishTrack:
        return "publish_track";
    case MediaTransportIntent::ConsumeTrack:
        return "consume_track";
    case MediaTransportIntent::ApplyOffer:
        return "apply_offer";
    case MediaTransportIntent::ApplyIce:
        return "apply_ice";
    case MediaTransportIntent::CloseSession:
        return "close_session";
    }
    return "unknown";
}

} // namespace eds::server_new::mediasoup::service
