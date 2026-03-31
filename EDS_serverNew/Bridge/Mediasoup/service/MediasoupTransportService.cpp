#include "Bridge/Mediasoup/service/MediasoupTransportService.h"

#include "Bridge/Mediasoup/service/MediasoupSfuEngine.h"

#include <utility>

namespace eds::server_new::mediasoup::service {

MediasoupTransportService::MediasoupTransportService(
    std::shared_ptr<IMediaEngine> engine,
    bool debugMode)
    : engine_(std::move(engine)) {
    if (!engine_) {
        engine_ = std::make_shared<MediasoupSfuEngine>(debugMode);
    }
}

MediasoupTransportService::~MediasoupTransportService() = default;

core::contracts::OperationStatus MediasoupTransportService::execute(
    MediaTransportIntent intent,
    const MediaTransportCommand& command,
    std::vector<MediaTransportEvent>& emittedEvents) {
    if (!engine_) {
        emittedEvents.clear();
        return core::contracts::OperationStatus::failure("Media engine is not configured.");
    }
    return engine_->execute(intent, command, emittedEvents);
}

std::vector<MediaSignalingEvent> MediasoupTransportService::consumeSignalingEventsForPeer(std::string_view peerId) {
    if (!engine_) {
        return {};
    }
    return engine_->consumeSignalingEventsForPeer(peerId);
}

void MediasoupTransportService::onSessionDisconnected(
    std::string_view peerId,
    std::uintptr_t sessionHandle,
    std::vector<MediaTransportEvent>& emittedEvents) {
    if (!engine_) {
        emittedEvents.clear();
        return;
    }
    engine_->onSessionDisconnected(peerId, sessionHandle, emittedEvents);
}

} // namespace eds::server_new::mediasoup::service
