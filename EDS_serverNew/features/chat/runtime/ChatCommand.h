#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace eds::server_new::features::chat {

    struct ChatCommand {
        std::uintptr_t sessionHandle = 0;
        std::string sessionId;
        std::string peerId;
        std::string userId;
        std::string conferenceId;
        std::string targetUserId;
        std::string targetPeerId;
        std::string clientRequestId;
        std::string text;
        std::size_t limit = 50;
        std::string beforeCreatedAt;
        std::string afterCreatedAt;
        std::vector<std::string> messageIds;
        bool markRead = false;
    };

    inline constexpr std::string_view kChatRouteObject = "chat";
    inline constexpr std::string_view kChatMessagingAgent = "messaging";
    inline constexpr std::string_view kActionSendMessage = "send_message";
    inline constexpr std::string_view kActionSyncMessages = "sync_messages";
    inline constexpr std::string_view kActionAckMessages = "ack_messages";

} // namespace eds::server_new::features::chat