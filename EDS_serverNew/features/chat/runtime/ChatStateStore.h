#pragma once

#include "contracts/Primitives.h"
#include "features/chat/runtime/ChatCommand.h"
#include "features/events/ConferenceEvents.h"

#include <cstdint>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace eds::server_new::features::chat {

class ChatStateStore {
public:
    struct OutboundChatEvent {
        std::string targetPeerId;
        nlohmann::json payload = nlohmann::json::object();
    };

    void applyConferenceSnapshot(const eds::server_new::features::events::ConferenceMembersSnapshotEvent& event);
    core::contracts::OperationStatus sendMessage(const ChatCommand& command);
    std::vector<OutboundChatEvent> consumeOutboundEventsForPeer(std::string_view senderPeerId);

private:
    struct ConferenceChatState {
        std::unordered_set<std::string> members;
        bool isClosed = false;
        std::uint64_t revision = 0;
    };

    static core::contracts::OperationStatus requireField(bool condition, std::string fieldName);
    static std::string makeIdempotencyKey(
        std::string_view conferenceId,
        std::string_view peerId,
        std::string_view clientRequestId);
    std::vector<std::string> resolveRecipientsNoLock(
        const ConferenceChatState& conference,
        std::string_view senderPeerId,
        std::string_view targetPeerId) const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, ConferenceChatState> conferences_;
    std::unordered_set<std::string> idempotencyKeys_;
    std::unordered_map<std::string, std::vector<OutboundChatEvent>> pendingEventsBySender_;
    std::uint64_t sequence_ = 0;
};

} // namespace eds::server_new::features::chat
