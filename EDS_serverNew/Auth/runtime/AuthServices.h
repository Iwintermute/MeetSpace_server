#pragma once

#include "Auth/ISupabaseAuthVerifier.h"
#include "Auth/SessionAuthStore.h"

#include <memory>
#include <string>

namespace eds::server_new::auth {

    class AuthServices final {
    public:
        static std::shared_ptr<SessionAuthStore> sessionStore();
        static std::shared_ptr<ISupabaseAuthVerifier> verifier();

        static void configure(std::string projectUrl, std::string publishableKey);
        static void setAllowDevAuthTokens(bool enabled);
        static bool allowDevAuthTokens();

    private:
        AuthServices() = delete;
    };

} // namespace eds::server_new::auth
