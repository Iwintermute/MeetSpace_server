#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace eds::server_new::features::events {

struct ConferenceMembersSnapshotEvent {
    std::string conferenceId;
    std::string ownerPeerId;
    bool isClosed = false;
    std::uint64_t revision = 0;
    std::vector<std::string> memberPeerIds;
};

} // namespace eds::server_new::features::events
