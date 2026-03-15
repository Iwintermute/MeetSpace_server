#pragma once

#include "Bridge/Mediasoup/runtime/MediasoupCommand.h"
#include "contracts/Primitives.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Sys::Rtc {
class cRtcManager;
}

namespace eds::server_new::mediasoup {

struct MediasoupSignalingEvent {
    std::string type;
    std::string peerId;
    std::string sdp;
    std::string sdpMid;
    std::string candidate;
};

class MediasoupRtcBridge {
public:
    using tOnPeerBinary = std::function<void(std::string_view, const std::vector<uint8_t>&)>;
    MediasoupRtcBridge();
    ~MediasoupRtcBridge();

    core::contracts::OperationStatus handleOffer(const MediasoupCommand& command);
    core::contracts::OperationStatus handleIce(const MediasoupCommand& command);
    core::contracts::OperationStatus handleClose(const MediasoupCommand& command);
    void onSessionDisconnected(std::string_view peerId, std::uintptr_t sessionHandle);
    void setOnPeerBinary(tOnPeerBinary callback);
    core::contracts::OperationStatus sendBinaryToPeer(
        std::string_view targetPeerId,
        const std::vector<uint8_t>& data);

    std::vector<MediasoupSignalingEvent> consumeEventsForPeer(std::string_view peerId);

private:
    core::contracts::OperationStatus validateSessionBoundCommand(const MediasoupCommand& command) const;
    void rememberSessionMapping(std::string_view peerId, std::uintptr_t sessionHandle);
    void cleanupSessionMapping(std::string_view peerId, std::uintptr_t sessionHandle);
    void enqueueEvent(std::uintptr_t sessionHandle, MediasoupSignalingEvent event);

private:
    std::shared_ptr<Sys::Rtc::cRtcManager> rtcManager_;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::uintptr_t> sessionByPeer_;
    std::unordered_map<std::uintptr_t, std::string> peerBySession_;
    std::unordered_map<std::string, std::vector<MediasoupSignalingEvent>> pendingEventsByPeer_;
    tOnPeerBinary onPeerBinary_;
};

} // namespace eds::server_new::mediasoup
