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

    roomIt->second.peers.insert(command.peerId);
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

bool MediasoupStateStore::hasPeerInRoomNoLock(std::string_view roomId, std::string_view peerId) const {
    auto roomIt = rooms_.find(std::string(roomId));
    if (roomIt == rooms_.end()) {
        return false;
    }
    return roomIt->second.peers.find(std::string(peerId)) != roomIt->second.peers.end();
}

} // namespace eds::server_new::mediasoup
