#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace eds::server_new::features::conference {

struct ConferenceCommand {
    std::uintptr_t sessionHandle = 0;
    std::string sessionId;
    std::string peerId;
    std::string conferenceId;
    std::string clientRequestId;
    std::string targetPeerId;
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

} // namespace eds::server_new::features::conference
