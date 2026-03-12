#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace eds::server_new::features::chat {

struct ChatCommand {
    std::uintptr_t sessionHandle = 0;
    std::string sessionId;
    std::string peerId;
    std::string conferenceId;
    std::string targetPeerId;
    std::string clientRequestId;
    std::string text;
};

inline constexpr std::string_view kChatRouteObject = "chat";
inline constexpr std::string_view kChatMessagingAgent = "messaging";
inline constexpr std::string_view kActionSendMessage = "send_message";

} // namespace eds::server_new::features::chat
