#pragma once

#include "Auth/ISupabaseAuthVerifier.h"
#include "Auth/SessionAuthStore.h"
#include "Auth/SupabaseAuthVerifier.h"

#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>

namespace eds::server_new::auth {

    class AuthServices {
    public:
        static std::shared_ptr<SessionAuthStore> sessionStore() {
            static std::shared_ptr<SessionAuthStore> instance = std::make_shared<SessionAuthStore>();
            return instance;
        }

        static std::shared_ptr<ISupabaseAuthVerifier> verifier() {
            static std::shared_ptr<ISupabaseAuthVerifier> instance = [] {
                const auto urlEnv = std::getenv("SUPABASE_URL");
                const auto keyEnv = std::getenv("SUPABASE_ANON_KEY");
                return std::static_pointer_cast<ISupabaseAuthVerifier>(
                    std::make_shared<SupabaseAuthVerifier>(
                        urlEnv ? std::string(urlEnv) : std::string(),
                        keyEnv ? std::string(keyEnv) : std::string()));
                }();
            return instance;
        }
    };

} // namespace eds::server_new::auth