#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace eds::server_new::features::conference {

    struct ConferenceCommand {
        std::uintptr_t sessionHandle = 0;
        std::string sessionId;
        std::string peerId;
        std::string userId;
        std::string conferenceId;
        std::string clientRequestId;
    };

    inline constexpr std::string_view kConferenceRouteObject = "conference";
    inline constexpr std::string_view kConferenceLifecycleAgent = "lifecycle";
    inline constexpr std::string_view kConferenceMembershipAgent = "membership";

    inline constexpr std::string_view kActionCreateConference = "create_conference";
    inline constexpr std::string_view kActionGetConference = "get_conference";
    inline constexpr std::string_view kActionCloseConference = "close_conference";
    inline constexpr std::string_view kActionJoinConference = "join_conference";
    inline constexpr std::string_view kActionLeaveConference = "leave_conference";
    inline constexpr std::string_view kActionListMembers = "list_members";
    inline constexpr std::string_view kActionListUserConferences = "list_user_conferences";
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

} // namespace eds::server_new::features::conference