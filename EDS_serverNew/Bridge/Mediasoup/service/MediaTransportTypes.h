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
    ConsumeTrack,
    ApplyOffer,
    ApplyIce,
    CloseSession
};

enum class MediaTransportEventType {
    SessionStarted,
    SessionEnded,
    TransportOpened,
    TrackPublished,
    TrackConsumed,
    SignalingAnswer,
    SignalingIce,
    SessionClosed,
    TransportError
};

inline std::string_view toString(MediaTransportEventType type) noexcept {
    switch (type) {
    case MediaTransportEventType::SessionStarted:
        return "session_started";
    case MediaTransportEventType::SessionEnded:
        return "session_ended";
    case MediaTransportEventType::TransportOpened:
        return "transport_opened";
    case MediaTransportEventType::TrackPublished:
        return "track_published";
    case MediaTransportEventType::TrackConsumed:
        return "track_consumed";
    case MediaTransportEventType::SignalingAnswer:
        return "signaling_answer";
    case MediaTransportEventType::SignalingIce:
        return "signaling_ice";
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
    std::string kind;
    std::string sdp;
    std::string sdpMid;
    std::string candidate;
    std::string correlationId;
};

struct MediaTransportEvent {
    MediaTransportEventType type = MediaTransportEventType::TransportError;
    std::string correlationId;
    std::string peerId;
    std::string roomId;
    std::string transportId;
    std::string producerId;
    std::string kind;
    std::string sdp;
    std::string sdpMid;
    std::string candidate;
    std::string reason;
    bool started = false;
    bool ended = false;
    std::vector<std::string> memberPeerIds;
    std::vector<std::string> notifyPeerIds;
};

struct MediaSignalingEvent {
    std::string type;
    std::string peerId;
    std::string sdp;
    std::string sdpMid;
    std::string candidate;
};

} // namespace eds::server_new::mediasoup::service
