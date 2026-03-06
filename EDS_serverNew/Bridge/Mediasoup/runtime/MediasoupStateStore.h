#pragma once

#include "Bridge/Mediasoup/runtime/MediasoupCommand.h"
#include "contracts/Primitives.h"

#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace eds::server_new::mediasoup {

class MediasoupStateStore {
public:
    core::contracts::OperationStatus createRoom(const MediasoupCommand& command);
    core::contracts::OperationStatus joinRoom(const MediasoupCommand& command);
    core::contracts::OperationStatus openTransport(const MediasoupCommand& command);
    core::contracts::OperationStatus startProducing(const MediasoupCommand& command);
    core::contracts::OperationStatus consume(const MediasoupCommand& command) const;

private:
    struct RoomState {
        std::unordered_set<std::string> peers;
    };

    struct TransportState {
        std::string roomId;
        std::string peerId;
    };

    struct ProducerState {
        std::string roomId;
        std::string peerId;
        std::string transportId;
        std::string kind;
    };

    bool hasPeerInRoomNoLock(std::string_view roomId, std::string_view peerId) const;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, RoomState> rooms_;
    std::unordered_map<std::string, TransportState> transports_;
    std::unordered_map<std::string, ProducerState> producers_;
};

} // namespace eds::server_new::mediasoup
