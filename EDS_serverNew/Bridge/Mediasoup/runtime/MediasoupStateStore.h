#pragma once

#include "Bridge/Mediasoup/runtime/MediasoupCommand.h"
#include "contracts/Primitives.h"

#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace eds::server_new::mediasoup {

class MediasoupStateStore {
public:
    struct SessionLifecycleNotification {
        std::string roomId;
        std::string actorPeerId;
        bool started = false;
        bool ended = false;
        std::string reason;
        std::vector<std::string> memberPeerIds;
        std::vector<std::string> notifyPeerIds;
    };
    core::contracts::OperationStatus createRoom(const MediasoupCommand& command);
    core::contracts::OperationStatus joinRoom(const MediasoupCommand& command);
    core::contracts::OperationStatus leaveRoom(const MediasoupCommand& command);
    core::contracts::OperationStatus openTransport(const MediasoupCommand& command);
    core::contracts::OperationStatus startProducing(const MediasoupCommand& command);
    core::contracts::OperationStatus consume(const MediasoupCommand& command) const;
    std::vector<std::string> listOtherPeersInSameRoom(std::string_view peerId) const;
    void disconnectPeer(
        std::string_view peerId,
        std::vector<SessionLifecycleNotification>& notifications);
    std::vector<SessionLifecycleNotification> consumeLifecycleNotificationsForPeer(std::string_view actorPeerId);

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
    std::vector<std::string> collectPeersNoLock(std::string_view roomId) const;
    void clearPeerRuntimeNoLock(std::string_view roomId, std::string_view peerId);
    void queueNotificationNoLock(std::string_view actorPeerId, SessionLifecycleNotification notification);

    mutable std::mutex mutex_;
    std::unordered_map<std::string, RoomState> rooms_;
    std::unordered_map<std::string, TransportState> transports_;
    std::unordered_map<std::string, ProducerState> producers_;
    std::unordered_map<std::string, std::vector<SessionLifecycleNotification>> pendingNotificationsByActor_;
};

} // namespace eds::server_new::mediasoup
