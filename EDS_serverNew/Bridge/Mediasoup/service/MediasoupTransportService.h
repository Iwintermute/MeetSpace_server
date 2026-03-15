#pragma once

#include "Bridge/Mediasoup/runtime/MediasoupCommand.h"
#include "Bridge/Mediasoup/runtime/MediasoupRtcBridge.h"
#include "Bridge/Mediasoup/runtime/MediasoupStateStore.h"
#include "Bridge/Mediasoup/service/IMediaTransportService.h"
#include <chrono>
#include <cstdint>

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace eds::server_new::mediasoup::service {

class MediasoupTransportService final : public IMediaTransportService {
public:
    MediasoupTransportService(
        std::shared_ptr<eds::server_new::mediasoup::MediasoupStateStore> stateStore = nullptr,
        std::shared_ptr<eds::server_new::mediasoup::MediasoupRtcBridge> rtcBridge = nullptr,
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
    struct PeerAudioRelayStats {
        std::uint64_t producedPackets = 0;
        std::uint64_t producedBytes = 0;
        std::uint64_t forwardedCopies = 0;
        std::uint64_t forwardedBytes = 0;
        std::uint64_t receivedCopies = 0;
        std::uint64_t receivedBytes = 0;
        std::uint64_t droppedPackets = 0;
        std::uint64_t failedDeliveries = 0;
    };
    struct AudioRelayStats {
        std::uint64_t ingressPackets = 0;
        std::uint64_t ingressBytes = 0;
        std::uint64_t forwardedCopies = 0;
        std::uint64_t forwardedBytes = 0;
        std::uint64_t droppedPackets = 0;
        std::uint64_t failedDeliveries = 0;
        std::chrono::steady_clock::time_point lastSummaryAt = std::chrono::steady_clock::now();
        std::unordered_map<std::string, PeerAudioRelayStats> perPeer;
    };
    void relayPeerBinaryPayload(std::string_view sourcePeerId, const std::vector<uint8_t>& payload);
    eds::server_new::mediasoup::MediasoupCommand toMediasoupCommand(const MediaTransportCommand& command) const;
    void appendLifecycleEventsForPeer(
        std::string_view actorPeerId,
        std::string_view correlationId,
        std::vector<MediaTransportEvent>& emittedEvents);
    MediaTransportEvent makeErrorEvent(
        const MediaTransportCommand& command,
        std::string reason) const;
    void logCommandResult(
        MediaTransportIntent intent,
        const MediaTransportCommand& command,
        const core::contracts::OperationStatus& status) const;
    void logAudioRelaySummaryIfNeeded() const;
    static std::string_view intentToString(MediaTransportIntent intent) noexcept;

private:
    std::shared_ptr<eds::server_new::mediasoup::MediasoupStateStore> stateStore_;
    std::shared_ptr<eds::server_new::mediasoup::MediasoupRtcBridge> rtcBridge_;
    bool debugMode_ = false;
    mutable std::mutex relayStatsMutex_;
    mutable AudioRelayStats relayStats_;
};

} // namespace eds::server_new::mediasoup::service
