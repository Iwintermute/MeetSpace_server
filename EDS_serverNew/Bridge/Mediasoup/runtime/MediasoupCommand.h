#pragma once

#include <string_view>
#include <string>

namespace eds::server_new::mediasoup {

struct MediasoupCommand {
    std::string sessionId;
    std::string peerId;
    std::string roomId;
    std::string transportId;
    std::string producerId;
    std::string kind;
};

inline constexpr std::string_view kRouteObject = "mediasoup";
inline constexpr std::string_view kDefaultAgent = "signaling";
inline constexpr std::string_view kActionCreateRoom = "create_room";
inline constexpr std::string_view kActionJoinRoom = "join_room";
inline constexpr std::string_view kActionOpenTransport = "open_transport";
inline constexpr std::string_view kActionProduce = "produce";
inline constexpr std::string_view kActionConsume = "consume";

} // namespace eds::server_new::mediasoup
