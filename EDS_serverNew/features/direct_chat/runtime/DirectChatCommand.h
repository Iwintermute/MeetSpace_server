#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace eds::server_new::features::direct_chat {

    struct DirectChatCommand {
        std::uintptr_t sessionHandle = 0;
        std::string sessionId;
        std::string peerId;
        std::string userId;
        std::string targetUserId;
        std::string targetPeerId;
        std::string threadId;
        std::string clientRequestId;
        std::string text;
        std::size_t limit = 50;
        std::string beforeCreatedAt;
        std::string afterCreatedAt;
        std::vector<std::string> messageIds;
        bool markRead = false;
    };

    inline constexpr std::string_view kDirectChatRouteObject = "direct_chat";
    inline constexpr std::string_view kDirectChatMessagingAgent = "messaging";
    inline constexpr std::string_view kActionSendDirectMessage = "send_message";
    inline constexpr std::string_view kActionSyncDirectMessages = "sync_messages";
    inline constexpr std::string_view kActionListDirectThreads = "list_threads";
    inline constexpr std::string_view kActionAckDirectMessages = "ack_messages";

} // namespace eds::server_new::features::direct_chat