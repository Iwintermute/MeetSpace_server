#pragma once

#include <optional>
#include <string>

namespace eds::server_new::auth {

    struct VerifiedSupabaseUser {
        std::string userId;
        std::string email;
    };

    class ISupabaseAuthVerifier {
    public:
        virtual ~ISupabaseAuthVerifier() = default;
        virtual std::optional<VerifiedSupabaseUser> verifyAccessToken(const std::string& accessToken) = 0;
    };

} // namespace eds::server_new::auth