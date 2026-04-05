#pragma once
#include <cstdint>

#include <string_view>
#include <string>

namespace eds::server_new::mediasoup {

struct MediasoupCommand {
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
    std::string dtlsParameters;
    std::string rtpParameters;
    std::string rtpCapabilities;
    bool injectTestRtp = false;
    std::int32_t testRtpPacketCount = 0;
    std::int32_t testRtpPayloadSize = 0;
    std::int32_t testRtpTimestampStep = 0;
};

inline constexpr std::string_view kRouteObject = "mediasoup";
inline constexpr std::string_view kDefaultAgent = "signaling";
inline constexpr std::string_view kActionCreateRoom = "create_room";
inline constexpr std::string_view kActionJoinRoom = "join_room";
inline constexpr std::string_view kActionLeaveRoom = "leave_room";
inline constexpr std::string_view kActionOpenTransport = "open_transport";
inline constexpr std::string_view kActionProduce = "produce";
inline constexpr std::string_view kActionConsume = "consume";
inline constexpr std::string_view kActionWebrtcOffer = "webrtc_offer";
inline constexpr std::string_view kActionWebrtcIce = "webrtc_ice";
inline constexpr std::string_view kActionWebrtcClose = "webrtc_close";
inline constexpr std::string_view kActionStats = "stats";
inline constexpr std::string_view kActionConnectSession = "connect_session";
inline constexpr std::string_view kActionDisconnectSession = "disconnect_session";

} // namespace eds::server_new::mediasoup
