#pragma once

#include "contracts/Primitives.h"
#include "features/conference/runtime/ConferenceCommand.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace eds::server_new::features::conference {

class ConferenceStateStore {
public:
    struct ConferenceSnapshot {
        std::string conferenceId;
        std::string ownerPeerId;
        bool isClosed = false;
        std::uint64_t revision = 0;
        std::vector<std::string> memberPeerIds;
    };
    core::contracts::OperationStatus createConference(const ConferenceCommand& command);
    core::contracts::OperationStatus getConference(const ConferenceCommand& command) const;
    core::contracts::OperationStatus closeConference(const ConferenceCommand& command);

    core::contracts::OperationStatus joinConference(const ConferenceCommand& command);
    core::contracts::OperationStatus leaveConference(const ConferenceCommand& command);
    core::contracts::OperationStatus listMembers(const ConferenceCommand& command) const;

    bool isPeerInConference(std::string_view conferenceId, std::string_view peerId) const;
    bool tryGetConferenceSnapshot(std::string_view conferenceId, ConferenceSnapshot& snapshot) const;
    std::vector<std::string> listConferenceIdsForPeer(std::string_view peerId) const;

private:
    struct ConferenceMemberState {
        std::string peerId;
        std::string sessionId;
        std::uintptr_t sessionHandle = 0;
        bool isOwner = false;
    };

    struct ConferenceState {
        std::string conferenceId;
        std::string ownerPeerId;
        bool isClosed = false;
        std::uint64_t revision = 0;
        std::unordered_map<std::string, ConferenceMemberState> membersByPeer;
    };

    static core::contracts::OperationStatus requireField(bool condition, std::string fieldName);
    bool hasMemberNoLock(std::string_view conferenceId, std::string_view peerId) const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, ConferenceState> conferences_;
    std::unordered_map<std::string, std::string> createIdempotencyIndex_;
};

} // namespace eds::server_new::features::conference
