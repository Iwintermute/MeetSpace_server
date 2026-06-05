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
    inline constexpr std::string_view kActionFileOffer = "file_offer";
    inline constexpr std::string_view kActionFileAccept = "file_accept";
    inline constexpr std::string_view kActionFileChunk = "file_chunk";
    inline constexpr std::string_view kActionFileComplete = "file_complete";
    inline constexpr std::string_view kActionFileCancel = "file_cancel";
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

    inline constexpr std::string_view kEventDirectCallFileOffer = "direct_call_file_offer";
    inline constexpr std::string_view kEventDirectCallFileAccept = "direct_call_file_accept";
    inline constexpr std::string_view kEventDirectCallFileChunk = "direct_call_file_chunk";
    inline constexpr std::string_view kEventDirectCallFileComplete = "direct_call_file_complete";
    inline constexpr std::string_view kEventDirectCallFileCancel = "direct_call_file_cancel";

} // namespace eds::server_new::features::direct_call