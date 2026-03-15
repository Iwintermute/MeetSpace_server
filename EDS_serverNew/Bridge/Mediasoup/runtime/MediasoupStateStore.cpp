#include "Bridge/Mediasoup/runtime/MediasoupStateStore.h"

namespace {
core::contracts::OperationStatus requireField(bool condition, std::string fieldName) {
    if (!condition) {
        return core::contracts::OperationStatus::failure(fieldName + " must not be empty.");
    }
    return core::contracts::OperationStatus::success();
}
}

namespace eds::server_new::mediasoup {

core::contracts::OperationStatus MediasoupStateStore::createRoom(const MediasoupCommand& command) {
    const auto roomValidation = requireField(!command.roomId.empty(), "roomId");
    if (!roomValidation.ok) {
        return roomValidation;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (rooms_.find(command.roomId) != rooms_.end()) {
        return core::contracts::OperationStatus::failure("Room already exists: " + command.roomId);
    }

    rooms_.emplace(command.roomId, RoomState{});
    return core::contracts::OperationStatus::success();
}

core::contracts::OperationStatus MediasoupStateStore::joinRoom(const MediasoupCommand& command) {
    const auto roomValidation = requireField(!command.roomId.empty(), "roomId");
    if (!roomValidation.ok) {
        return roomValidation;
    }
    const auto peerValidation = requireField(!command.peerId.empty(), "peerId");
    if (!peerValidation.ok) {
        return peerValidation;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto roomIt = rooms_.find(command.roomId);
    if (roomIt == rooms_.end()) {
        return core::contracts::OperationStatus::failure("Room not found: " + command.roomId);
    }

    const auto peerCountBeforeJoin = roomIt->second.peers.size();
    const auto inserted = roomIt->second.peers.insert(command.peerId).second;
    if (inserted && peerCountBeforeJoin == 0) {
        SessionLifecycleNotification notification;
        notification.roomId = command.roomId;
        notification.actorPeerId = command.peerId;
        notification.started = true;
        notification.reason = "joined";
        notification.memberPeerIds = collectPeersNoLock(command.roomId);
        notification.notifyPeerIds = notification.memberPeerIds;
        queueNotificationNoLock(command.peerId, std::move(notification));
    }

    return core::contracts::OperationStatus::success();
}

core::contracts::OperationStatus MediasoupStateStore::leaveRoom(const MediasoupCommand& command) {
    const auto roomValidation = requireField(!command.roomId.empty(), "roomId");
    if (!roomValidation.ok) {
        return roomValidation;
    }
    const auto peerValidation = requireField(!command.peerId.empty(), "peerId");
    if (!peerValidation.ok) {
        return peerValidation;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto roomIt = rooms_.find(command.roomId);
    if (roomIt == rooms_.end()) {
        return core::contracts::OperationStatus::failure("Room not found: " + command.roomId);
    }

    auto peerIt = roomIt->second.peers.find(command.peerId);
    if (peerIt == roomIt->second.peers.end()) {
        return core::contracts::OperationStatus::failure("Peer is not joined to room.");
    }

    const auto peerCountBeforeLeave = roomIt->second.peers.size();
    roomIt->second.peers.erase(peerIt);
    clearPeerRuntimeNoLock(command.roomId, command.peerId);

    const auto peerCountAfterLeave = roomIt->second.peers.size();
    if (peerCountBeforeLeave > 0 && peerCountAfterLeave == 0) {
        SessionLifecycleNotification notification;
        notification.roomId = command.roomId;
        notification.actorPeerId = command.peerId;
        notification.ended = true;
        notification.reason = "left";
        notification.memberPeerIds = collectPeersNoLock(command.roomId);
        notification.notifyPeerIds.push_back(command.peerId);
        queueNotificationNoLock(command.peerId, std::move(notification));
    }

    return core::contracts::OperationStatus::success();
}

core::contracts::OperationStatus MediasoupStateStore::openTransport(const MediasoupCommand& command) {
    const auto roomValidation = requireField(!command.roomId.empty(), "roomId");
    if (!roomValidation.ok) {
        return roomValidation;
    }
    const auto peerValidation = requireField(!command.peerId.empty(), "peerId");
    if (!peerValidation.ok) {
        return peerValidation;
    }
    const auto transportValidation = requireField(!command.transportId.empty(), "transportId");
    if (!transportValidation.ok) {
        return transportValidation;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (!hasPeerInRoomNoLock(command.roomId, command.peerId)) {
        return core::contracts::OperationStatus::failure("Peer is not joined to room.");
    }

    if (transports_.find(command.transportId) != transports_.end()) {
        return core::contracts::OperationStatus::failure("Transport already exists: " + command.transportId);
    }

    transports_.emplace(command.transportId, TransportState{ command.roomId, command.peerId });
    return core::contracts::OperationStatus::success();
}

core::contracts::OperationStatus MediasoupStateStore::startProducing(const MediasoupCommand& command) {
    const auto roomValidation = requireField(!command.roomId.empty(), "roomId");
    if (!roomValidation.ok) {
        return roomValidation;
    }
    const auto peerValidation = requireField(!command.peerId.empty(), "peerId");
    if (!peerValidation.ok) {
        return peerValidation;
    }
    const auto transportValidation = requireField(!command.transportId.empty(), "transportId");
    if (!transportValidation.ok) {
        return transportValidation;
    }
    const auto producerValidation = requireField(!command.producerId.empty(), "producerId");
    if (!producerValidation.ok) {
        return producerValidation;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto transportIt = transports_.find(command.transportId);
    if (transportIt == transports_.end()) {
        return core::contracts::OperationStatus::failure("Transport not found: " + command.transportId);
    }
    if (transportIt->second.roomId != command.roomId || transportIt->second.peerId != command.peerId) {
        return core::contracts::OperationStatus::failure("Transport does not belong to specified room/peer.");
    }
    if (producers_.find(command.producerId) != producers_.end()) {
        return core::contracts::OperationStatus::failure("Producer already exists: " + command.producerId);
    }

    producers_.emplace(command.producerId, ProducerState{
        command.roomId,
        command.peerId,
        command.transportId,
        command.kind
    });
    return core::contracts::OperationStatus::success();
}

core::contracts::OperationStatus MediasoupStateStore::consume(const MediasoupCommand& command) const {
    const auto roomValidation = requireField(!command.roomId.empty(), "roomId");
    if (!roomValidation.ok) {
        return roomValidation;
    }
    const auto peerValidation = requireField(!command.peerId.empty(), "peerId");
    if (!peerValidation.ok) {
        return peerValidation;
    }
    const auto producerValidation = requireField(!command.producerId.empty(), "producerId");
    if (!producerValidation.ok) {
        return producerValidation;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (!hasPeerInRoomNoLock(command.roomId, command.peerId)) {
        return core::contracts::OperationStatus::failure("Peer is not joined to room.");
    }

    auto producerIt = producers_.find(command.producerId);
    if (producerIt == producers_.end()) {
        return core::contracts::OperationStatus::failure("Producer not found: " + command.producerId);
    }
    if (producerIt->second.roomId != command.roomId) {
        return core::contracts::OperationStatus::failure("Producer is registered in another room.");
    }

    return core::contracts::OperationStatus::success();
}

std::vector<std::string> MediasoupStateStore::listOtherPeersInSameRoom(std::string_view peerId) const {
    std::vector<std::string> recipients;
    if (peerId.empty()) {
        return recipients;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [roomId, roomState] : rooms_) {
        static_cast<void>(roomId);
        if (roomState.peers.find(std::string(peerId)) == roomState.peers.end()) {
            continue;
        }
        recipients.reserve(roomState.peers.size());
        for (const auto& memberPeerId : roomState.peers) {
            if (memberPeerId != peerId) {
                recipients.push_back(memberPeerId);
            }
        }
        break;
    }
    return recipients;
}

void MediasoupStateStore::disconnectPeer(
    std::string_view peerId,
    std::vector<SessionLifecycleNotification>& notifications) {
    if (peerId.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [roomId, roomState] : rooms_) {
        auto peerIt = roomState.peers.find(std::string(peerId));
        if (peerIt == roomState.peers.end()) {
            continue;
        }

        const auto peerCountBeforeLeave = roomState.peers.size();
        roomState.peers.erase(peerIt);
        clearPeerRuntimeNoLock(roomId, peerId);

        const auto peerCountAfterLeave = roomState.peers.size();
        if (peerCountBeforeLeave > 0 && peerCountAfterLeave == 0) {
            SessionLifecycleNotification notification;
            notification.roomId = roomId;
            notification.actorPeerId = std::string(peerId);
            notification.ended = true;
            notification.reason = "disconnected";
            notification.memberPeerIds = collectPeersNoLock(roomId);
            notification.notifyPeerIds = notification.memberPeerIds;
            notifications.push_back(std::move(notification));
        }
    }

    pendingNotificationsByActor_.erase(std::string(peerId));
}

std::vector<MediasoupStateStore::SessionLifecycleNotification> MediasoupStateStore::consumeLifecycleNotificationsForPeer(
    std::string_view actorPeerId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto iterator = pendingNotificationsByActor_.find(std::string(actorPeerId));
    if (iterator == pendingNotificationsByActor_.end()) {
        return {};
    }

    auto notifications = std::move(iterator->second);
    pendingNotificationsByActor_.erase(iterator);
    return notifications;
}

bool MediasoupStateStore::hasPeerInRoomNoLock(std::string_view roomId, std::string_view peerId) const {
    auto roomIt = rooms_.find(std::string(roomId));
    if (roomIt == rooms_.end()) {
        return false;
    }
    return roomIt->second.peers.find(std::string(peerId)) != roomIt->second.peers.end();
}

std::vector<std::string> MediasoupStateStore::collectPeersNoLock(std::string_view roomId) const {
    std::vector<std::string> peers;
    auto roomIt = rooms_.find(std::string(roomId));
    if (roomIt == rooms_.end()) {
        return peers;
    }

    peers.reserve(roomIt->second.peers.size());
    for (const auto& peerId : roomIt->second.peers) {
        peers.push_back(peerId);
    }
    return peers;
}

void MediasoupStateStore::clearPeerRuntimeNoLock(std::string_view roomId, std::string_view peerId) {
    std::unordered_set<std::string> removedTransportIds;

    for (auto transportIt = transports_.begin(); transportIt != transports_.end();) {
        if (transportIt->second.roomId == roomId && transportIt->second.peerId == peerId) {
            removedTransportIds.insert(transportIt->first);
            transportIt = transports_.erase(transportIt);
            continue;
        }
        ++transportIt;
    }

    for (auto producerIt = producers_.begin(); producerIt != producers_.end();) {
        const bool sameRoomAndPeer =
            producerIt->second.roomId == roomId && producerIt->second.peerId == peerId;
        const bool transportRemoved =
            removedTransportIds.find(producerIt->second.transportId) != removedTransportIds.end();
        if (sameRoomAndPeer || transportRemoved) {
            producerIt = producers_.erase(producerIt);
            continue;
        }
        ++producerIt;
    }
}

void MediasoupStateStore::queueNotificationNoLock(
    std::string_view actorPeerId,
    SessionLifecycleNotification notification) {
    pendingNotificationsByActor_[std::string(actorPeerId)].push_back(std::move(notification));
}

} // namespace eds::server_new::mediasoup
