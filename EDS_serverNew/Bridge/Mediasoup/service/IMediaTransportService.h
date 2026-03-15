#pragma once

#include "Bridge/Mediasoup/service/MediaTransportTypes.h"
#include "contracts/Primitives.h"

#include <string_view>
#include <vector>

namespace eds::server_new::mediasoup::service {

class IMediaTransportService {
public:
    virtual ~IMediaTransportService() = default;

    virtual core::contracts::OperationStatus execute(
        MediaTransportIntent intent,
        const MediaTransportCommand& command,
        std::vector<MediaTransportEvent>& emittedEvents) = 0;

    virtual std::vector<MediaSignalingEvent> consumeSignalingEventsForPeer(std::string_view peerId) = 0;

    virtual void onSessionDisconnected(
        std::string_view peerId,
        std::uintptr_t sessionHandle,
        std::vector<MediaTransportEvent>& emittedEvents) = 0;
};

} // namespace eds::server_new::mediasoup::service
