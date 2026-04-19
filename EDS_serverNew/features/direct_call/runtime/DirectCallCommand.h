#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace eds::server_new::features::direct_call {

    struct DirectCallCommand {
        std::uintptr_t sessionHandle = 0;
        std::string sessionId;
        std::string peerId;
        std::string userId;
        std::string targetUserId;
        std::string callId;
        std::string clientRequestId;
    };

    inline constexpr std::string_view kDirectCallRouteObject = "direct_call";
    inline constexpr std::string_view kDirectCallLifecycleAgent = "lifecycle";

    inline constexpr std::string_view kActionCreateDirectCall = "create_call";
    inline constexpr std::string_view kActionAcceptDirectCall = "accept_call";
    inline constexpr std::string_view kActionDeclineDirectCall = "decline_call";
    inline constexpr std::string_view kActionHangupDirectCall = "hangup_call";
    inline constexpr std::string_view kActionListActiveCalls = "list_active_calls";
    inline constexpr std::string_view kActionOpenTransport = "open_transport";
    inline constexpr std::string_view kActionPublishTrack = "publish_track";
    inline constexpr std::string_view kActionPauseTrack = "pause_track";
    inline constexpr std::string_view kActionResumeTrack = "resume_track";
    inline constexpr std::string_view kActionCloseTrack = "close_track";
    inline constexpr std::string_view kActionConsumeTrack = "consume_track";
    inline constexpr std::string_view kActionConsumerReady = "consumer_ready";
    inline constexpr std::string_view kActionWebrtcOffer = "webrtc_offer";
    inline constexpr std::string_view kActionWebrtcIce = "webrtc_ice";
    inline constexpr std::string_view kActionWebrtcClose = "webrtc_close";
    inline constexpr std::string_view kActionMediaStats = "media_stats";

} // namespace eds::server_new::features::direct_call