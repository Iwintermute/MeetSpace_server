#pragma once

#include "auth/ISupabaseAuthVerifier.h"

#include <string>

namespace eds::server_new::auth {

    class SupabaseAuthVerifier final : public ISupabaseAuthVerifier {
    public:
        SupabaseAuthVerifier(std::string projectUrl, std::string publishableKey)
            : projectUrl_(std::move(projectUrl)),
            publishableKey_(std::move(publishableKey)) {
        }

        std::optional<VerifiedSupabaseUser> verifyAccessToken(const std::string& accessToken) override;

    private:
        std::string projectUrl_;
        std::string publishableKey_;
    };

} // namespace eds::server_new::auth