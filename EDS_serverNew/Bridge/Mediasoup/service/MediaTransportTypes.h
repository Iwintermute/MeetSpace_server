#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace eds::server_new::mediasoup::service {

    enum class MediaTransportIntent {
        CreateRoom,
        JoinSession,
        LeaveSession,
        OpenTransport,
        PublishTrack,
        PauseTrack,
        ResumeTrack,
        CloseTrack,
        ConsumeTrack,
        ResumeConsumer,
        ApplyOffer,
        ApplyIce,
        ReadStats,
        CloseSession
    };

    enum class MediaTransportEventType {
        RoomState,
        SessionStarted,
        SessionEnded,
        PeerJoined,
        PeerLeft,
        TransportOpened,
        TrackPublished,
        TrackClosed,
        ConsumerResumed,
        SessionClosed,
        TransportError
    };

    inline std::string_view toString(MediaTransportEventType type) noexcept {
        switch (type) {
        case MediaTransportEventType::RoomState:
            return "room_state";
        case MediaTransportEventType::SessionStarted:
            return "session_started";
        case MediaTransportEventType::SessionEnded:
            return "session_ended";
        case MediaTransportEventType::PeerJoined:
            return "peer_joined";
        case MediaTransportEventType::PeerLeft:
            return "peer_left";
        case MediaTransportEventType::TransportOpened:
            return "transport_opened";
        case MediaTransportEventType::TrackPublished:
            return "track_published";
        case MediaTransportEventType::TrackClosed:
            return "track_closed";
        case MediaTransportEventType::ConsumerResumed:
            return "consumer_resumed";
        case MediaTransportEventType::SessionClosed:
            return "session_closed";
        case MediaTransportEventType::TransportError:
            return "transport_error";
        }
        return "unknown";
    }

    struct MediaTransportCommand {
        std::uintptr_t sessionHandle = 0;
        std::string sessionId;
        std::string peerId;
        std::string roomId;
        std::string transportId;
        std::string producerId;
        std::string consumerId;
        std::string kind;
        std::string trackType;
        std::string sdp;
        std::string sdpMid;
        std::string candidate;
        std::string dtlsParameters;
        std::string rtpParameters;
        std::string rtpCapabilities;
        bool injectTestRtp = false;
        std::int32_t testRtpPacketCount = 0;
        std::int32_t testRtpPayloadSize = 0;
        std::int32_t testRtpTimestampStep = 0;
        std::string correlationId;
    };

    struct MediaProducerSnapshot {
        std::string producerId;
        std::string peerId;
        std::string kind;
        std::string trackType;
    };

    struct MediaTransportEvent {
        MediaTransportEventType type = MediaTransportEventType::TransportError;
        std::string correlationId;
        std::string peerId;
        std::string roomId;
        std::string transportId;
        std::string producerId;
        std::string consumerId;
        std::string producerPeerId;
        std::string kind;
        std::string trackType;
        std::string sdp;
        std::string sdpMid;
        std::string candidate;
        std::string reason;
        std::string rtpParameters;
        bool started = false;
        bool ended = false;
        bool paused = false;
        std::vector<std::string> memberPeerIds;
        std::vector<std::string> notifyPeerIds;
        std::vector<MediaProducerSnapshot> activeProducers;
    };

    struct MediaSignalingEvent {
        std::string type;
        std::string peerId;
        std::string sdp;
        std::string sdpMid;
        std::string candidate;
    };

} // namespace eds::server_new::mediasoup::service