#pragma once

#include <string>
#include <vector>

namespace eds::server_new::features::events {

struct AudioSessionLifecycleEvent {
    std::string roomId;
    std::string actorPeerId;
    bool started = false;
    bool ended = false;
    std::string reason;
    std::vector<std::string> memberPeerIds;
    std::vector<std::string> notifyPeerIds;
};

} // namespace eds::server_new::features::events
