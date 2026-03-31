#pragma once

#include "Bridge/Mediasoup/service/IMediaEngine.h"
#include "Bridge/Mediasoup/service/IMediaTransportService.h"

#include <memory>
#include <string_view>
#include <vector>

namespace eds::server_new::mediasoup::service {

class MediasoupTransportService final : public IMediaTransportService {
public:
    explicit MediasoupTransportService(
        std::shared_ptr<IMediaEngine> engine = nullptr,
        bool debugMode = false);
    ~MediasoupTransportService() override;

    core::contracts::OperationStatus execute(
        MediaTransportIntent intent,
        const MediaTransportCommand& command,
        std::vector<MediaTransportEvent>& emittedEvents) override;

    std::vector<MediaSignalingEvent> consumeSignalingEventsForPeer(std::string_view peerId) override;

    void onSessionDisconnected(
        std::string_view peerId,
        std::uintptr_t sessionHandle,
        std::vector<MediaTransportEvent>& emittedEvents) override;

private:
    std::shared_ptr<IMediaEngine> engine_;
};

} // namespace eds::server_new::mediasoup::service
