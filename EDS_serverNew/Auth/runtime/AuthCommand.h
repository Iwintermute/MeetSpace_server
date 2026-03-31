#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace eds::server_new::features::auth {

    struct AuthCommand {
        std::uintptr_t sessionHandle = 0;
        std::string trustedPeer;
        std::string accessToken;
        std::string deviceId;
    };

    inline constexpr std::string_view kAuthRouteObject = "auth";
    inline constexpr std::string_view kAuthSessionAgent = "session";
    inline constexpr std::string_view kActionBindSession = "bind_session";

    inline constexpr std::string_view kActionRestoreSession = "restore_session";
    inline constexpr std::string_view kActionLogoutSession = "logout_session";

} // namespace eds::server_new::features::auth