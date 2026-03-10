#include "features/conference/runtime/ConferenceStateStore.h"

#include <nlohmann/json.hpp>

namespace eds::server_new::features::conference {

namespace {
using json = nlohmann::json;
}

core::contracts::OperationStatus ConferenceStateStore::requireField(bool condition, std::string fieldName) {
    if (!condition) {
        return core::contracts::OperationStatus::failure(fieldName + " must not be empty.");
    }
    return core::contracts::OperationStatus::success();
}

core::contracts::OperationStatus ConferenceStateStore::createConference(const ConferenceCommand& command) {
    const auto conferenceValidation = requireField(!command.conferenceId.empty(), "conferenceId");
    if (!conferenceValidation.ok) {
        return conferenceValidation;
    }
    const auto peerValidation = requireField(!command.peerId.empty(), "peerId");
    if (!peerValidation.ok) {
        return peerValidation;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    if (!command.clientRequestId.empty()) {
        auto idempotencyIt = createIdempotencyIndex_.find(command.clientRequestId);
        if (idempotencyIt != createIdempotencyIndex_.end()) {
            if (idempotencyIt->second == command.conferenceId) {
                return core::contracts::OperationStatus::success();
            }
            return core::contracts::OperationStatus::failure("clientRequestId is already bound to another conferenceId.");
        }
    }

    if (conferences_.find(command.conferenceId) != conferences_.end()) {
        return core::contracts::OperationStatus::failure("Conference already exists: " + command.conferenceId);
    }

    ConferenceState conference;
    conference.conferenceId = command.conferenceId;
    conference.ownerPeerId = command.peerId;
    conference.isClosed = false;
    conference.revision = 1;
    conference.membersByPeer.emplace(
        command.peerId,
        ConferenceMemberState{
            command.peerId,
            command.sessionId,
            command.sessionHandle,
            true
        });

    conferences_.emplace(command.conferenceId, std::move(conference));
    if (!command.clientRequestId.empty()) {
        createIdempotencyIndex_[command.clientRequestId] = command.conferenceId;
    }
    return core::contracts::OperationStatus::success();
}

core::contracts::OperationStatus ConferenceStateStore::getConference(const ConferenceCommand& command) const {
    const auto conferenceValidation = requireField(!command.conferenceId.empty(), "conferenceId");
    if (!conferenceValidation.ok) {
        return conferenceValidation;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto conferenceIt = conferences_.find(command.conferenceId);
    if (conferenceIt == conferences_.end()) {
        return core::contracts::OperationStatus::failure("Conference not found: " + command.conferenceId);
    }
    if (!command.peerId.empty() && !hasMemberNoLock(command.conferenceId, command.peerId)) {
        return core::contracts::OperationStatus::failure("Peer is not a conference member.");
    }

    const auto& conference = conferenceIt->second;
    json response{
        { "conferenceId", conference.conferenceId },
        { "ownerPeerId", conference.ownerPeerId },
        { "isClosed", conference.isClosed },
        { "memberCount", conference.membersByPeer.size() },
        { "revision", conference.revision }
    };
    return { true, response.dump() };
}

core::contracts::OperationStatus ConferenceStateStore::closeConference(const ConferenceCommand& command) {
    const auto conferenceValidation = requireField(!command.conferenceId.empty(), "conferenceId");
    if (!conferenceValidation.ok) {
        return conferenceValidation;
    }
    const auto peerValidation = requireField(!command.peerId.empty(), "peerId");
    if (!peerValidation.ok) {
        return peerValidation;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto conferenceIt = conferences_.find(command.conferenceId);
    if (conferenceIt == conferences_.end()) {
        return core::contracts::OperationStatus::failure("Conference not found: " + command.conferenceId);
    }

    auto& conference = conferenceIt->second;
    if (conference.ownerPeerId != command.peerId) {
        return core::contracts::OperationStatus::failure("Only conference owner can close conference.");
    }

    if (!conference.isClosed) {
        conference.isClosed = true;
        conference.revision += 1;
    }
    return core::contracts::OperationStatus::success();
}

core::contracts::OperationStatus ConferenceStateStore::joinConference(const ConferenceCommand& command) {
    const auto conferenceValidation = requireField(!command.conferenceId.empty(), "conferenceId");
    if (!conferenceValidation.ok) {
        return conferenceValidation;
    }
    const auto peerValidation = requireField(!command.peerId.empty(), "peerId");
    if (!peerValidation.ok) {
        return peerValidation;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto conferenceIt = conferences_.find(command.conferenceId);
    if (conferenceIt == conferences_.end()) {
        return core::contracts::OperationStatus::failure("Conference not found: " + command.conferenceId);
    }

    auto& conference = conferenceIt->second;
    if (conference.isClosed) {
        return core::contracts::OperationStatus::failure("Conference is closed.");
    }

    auto memberIt = conference.membersByPeer.find(command.peerId);
    if (memberIt == conference.membersByPeer.end()) {
        conference.membersByPeer.emplace(
            command.peerId,
            ConferenceMemberState{
                command.peerId,
                command.sessionId,
                command.sessionHandle,
                false
            });
        conference.revision += 1;
        return core::contracts::OperationStatus::success();
    }

    memberIt->second.sessionId = command.sessionId;
    memberIt->second.sessionHandle = command.sessionHandle;
    return core::contracts::OperationStatus::success();
}

core::contracts::OperationStatus ConferenceStateStore::leaveConference(const ConferenceCommand& command) {
    const auto conferenceValidation = requireField(!command.conferenceId.empty(), "conferenceId");
    if (!conferenceValidation.ok) {
        return conferenceValidation;
    }
    const auto peerValidation = requireField(!command.peerId.empty(), "peerId");
    if (!peerValidation.ok) {
        return peerValidation;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto conferenceIt = conferences_.find(command.conferenceId);
    if (conferenceIt == conferences_.end()) {
        return core::contracts::OperationStatus::failure("Conference not found: " + command.conferenceId);
    }

    auto& conference = conferenceIt->second;
    auto memberIt = conference.membersByPeer.find(command.peerId);
    if (memberIt == conference.membersByPeer.end()) {
        return core::contracts::OperationStatus::failure("Peer is not a conference member.");
    }

    const bool ownerLeaving = memberIt->second.isOwner;
    conference.membersByPeer.erase(memberIt);
    conference.revision += 1;

    if (conference.membersByPeer.empty()) {
        conference.isClosed = true;
        return core::contracts::OperationStatus::success();
    }

    if (ownerLeaving) {
        auto newOwnerIt = conference.membersByPeer.begin();
        newOwnerIt->second.isOwner = true;
        conference.ownerPeerId = newOwnerIt->second.peerId;
    }

    return core::contracts::OperationStatus::success();
}

core::contracts::OperationStatus ConferenceStateStore::listMembers(const ConferenceCommand& command) const {
    const auto conferenceValidation = requireField(!command.conferenceId.empty(), "conferenceId");
    if (!conferenceValidation.ok) {
        return conferenceValidation;
    }
    const auto peerValidation = requireField(!command.peerId.empty(), "peerId");
    if (!peerValidation.ok) {
        return peerValidation;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto conferenceIt = conferences_.find(command.conferenceId);
    if (conferenceIt == conferences_.end()) {
        return core::contracts::OperationStatus::failure("Conference not found: " + command.conferenceId);
    }
    if (!hasMemberNoLock(command.conferenceId, command.peerId)) {
        return core::contracts::OperationStatus::failure("Peer is not a conference member.");
    }

    const auto& conference = conferenceIt->second;
    json members = json::array();
    for (const auto& [memberPeerId, memberState] : conference.membersByPeer) {
        members.push_back(json{
            { "peerId", memberPeerId },
            { "sessionId", memberState.sessionId },
            { "isOwner", memberState.isOwner }
        });
    }

    json response{
        { "conferenceId", conference.conferenceId },
        { "revision", conference.revision },
        { "members", std::move(members) }
    };
    return { true, response.dump() };
}

bool ConferenceStateStore::isPeerInConference(std::string_view conferenceId, std::string_view peerId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return hasMemberNoLock(conferenceId, peerId);
}

bool ConferenceStateStore::hasMemberNoLock(std::string_view conferenceId, std::string_view peerId) const {
    auto conferenceIt = conferences_.find(std::string(conferenceId));
    if (conferenceIt == conferences_.end()) {
        return false;
    }
    return conferenceIt->second.membersByPeer.find(std::string(peerId)) != conferenceIt->second.membersByPeer.end();
}

} // namespace eds::server_new::features::conference
