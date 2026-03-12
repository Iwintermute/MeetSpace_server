#include "features/chat/runtime/ChatStateStore.h"

#include <chrono>
#include <utility>

namespace eds::server_new::features::chat {

core::contracts::OperationStatus ChatStateStore::requireField(bool condition, std::string fieldName) {
    if (!condition) {
        return core::contracts::OperationStatus::failure(std::move(fieldName) + " must not be empty.");
    }
    return core::contracts::OperationStatus::success();
}

std::string ChatStateStore::makeIdempotencyKey(
    std::string_view conferenceId,
    std::string_view peerId,
    std::string_view clientRequestId) {
    std::string key;
    key.reserve(conferenceId.size() + peerId.size() + clientRequestId.size() + 2);
    key.append(conferenceId);
    key.push_back('|');
    key.append(peerId);
    key.push_back('|');
    key.append(clientRequestId);
    return key;
}

void ChatStateStore::applyConferenceSnapshot(const eds::server_new::features::events::ConferenceMembersSnapshotEvent& event) {
    if (event.conferenceId.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto& state = conferences_[event.conferenceId];
    if (event.revision < state.revision) {
        return;
    }

    state.revision = event.revision;
    state.isClosed = event.isClosed;
    state.members.clear();
    for (const auto& memberPeerId : event.memberPeerIds) {
        if (!memberPeerId.empty()) {
            state.members.insert(memberPeerId);
        }
    }
}

std::vector<std::string> ChatStateStore::resolveRecipientsNoLock(
    const ConferenceChatState& conference,
    std::string_view senderPeerId,
    std::string_view targetPeerId) const {
    std::vector<std::string> recipients;
    if (!targetPeerId.empty()) {
        recipients.push_back(std::string(targetPeerId));
        return recipients;
    }

    recipients.reserve(conference.members.size());
    for (const auto& memberPeerId : conference.members) {
        if (memberPeerId != senderPeerId) {
            recipients.push_back(memberPeerId);
        }
    }
    return recipients;
}

core::contracts::OperationStatus ChatStateStore::sendMessage(const ChatCommand& command) {
    const auto conferenceValidation = requireField(!command.conferenceId.empty(), "conferenceId");
    if (!conferenceValidation.ok) {
        return conferenceValidation;
    }
    const auto senderValidation = requireField(!command.peerId.empty(), "peerId");
    if (!senderValidation.ok) {
        return senderValidation;
    }
    const auto requestValidation = requireField(!command.clientRequestId.empty(), "clientRequestId");
    if (!requestValidation.ok) {
        return requestValidation;
    }
    const auto textValidation = requireField(!command.text.empty(), "text");
    if (!textValidation.ok) {
        return textValidation;
    }
    if (command.text.size() > 4000) {
        return core::contracts::OperationStatus::failure("text is too long. Max length is 4000 symbols.");
    }

    std::lock_guard<std::mutex> lock(mutex_);

    auto conferenceIt = conferences_.find(command.conferenceId);
    if (conferenceIt == conferences_.end()) {
        return core::contracts::OperationStatus::failure("conference not found in chat membership index.");
    }

    auto& conference = conferenceIt->second;
    if (conference.isClosed) {
        return core::contracts::OperationStatus::failure("conference is closed.");
    }
    if (conference.members.find(command.peerId) == conference.members.end()) {
        return core::contracts::OperationStatus::failure("sender is not conference member.");
    }
    if (!command.targetPeerId.empty() && conference.members.find(command.targetPeerId) == conference.members.end()) {
        return core::contracts::OperationStatus::failure("target peer is not conference member.");
    }

    const auto idempotencyKey = makeIdempotencyKey(command.conferenceId, command.peerId, command.clientRequestId);
    if (idempotencyKeys_.find(idempotencyKey) != idempotencyKeys_.end()) {
        return core::contracts::OperationStatus::success();
    }

    auto recipients = resolveRecipientsNoLock(conference, command.peerId, command.targetPeerId);
    const auto sentAtUnixMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    sequence_ += 1;
    const auto messageId = std::string("chat-") + std::to_string(sequence_);

    nlohmann::json messageEvent{
        { "type", "chat_message" },
        { "object", std::string(kChatRouteObject) },
        { "conferenceId", command.conferenceId },
        { "messageId", messageId },
        { "clientRequestId", command.clientRequestId },
        { "senderPeerId", command.peerId },
        { "targetPeerId", command.targetPeerId },
        { "text", command.text },
        { "sentAtUnixMs", sentAtUnixMs }
    };

    auto& outbound = pendingEventsBySender_[command.peerId];
    outbound.reserve(outbound.size() + recipients.size());
    for (const auto& recipientPeerId : recipients) {
        outbound.push_back(OutboundChatEvent{
            recipientPeerId,
            messageEvent
        });
    }

    idempotencyKeys_.insert(idempotencyKey);
    return core::contracts::OperationStatus::success();
}

std::vector<ChatStateStore::OutboundChatEvent> ChatStateStore::consumeOutboundEventsForPeer(std::string_view senderPeerId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pendingEventsBySender_.find(std::string(senderPeerId));
    if (it == pendingEventsBySender_.end()) {
        return {};
    }

    auto outbound = std::move(it->second);
    pendingEventsBySender_.erase(it);
    return outbound;
}

} // namespace eds::server_new::features::chat
