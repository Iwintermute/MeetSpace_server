#include "Auth/runtime/AuthServices.h"
#include "Auth/SupabaseAuthVerifier.h"

#include <cstdlib>
#include <mutex>
#include <utility>

namespace eds::server_new::auth {
    namespace {
        std::mutex gAuthServicesMutex;
        std::shared_ptr<SessionAuthStore> gSessionStore;
        std::shared_ptr<ISupabaseAuthVerifier> gVerifier;
        std::string gProjectUrl;
        std::string gPublishableKey;
        bool gAllowDevAuthTokens = false;

        std::string readEnv(const char* name) {
            const char* value = std::getenv(name);
            return value ? std::string(value) : std::string{};
        }
    } // namespace

    std::shared_ptr<SessionAuthStore> AuthServices::sessionStore() {
        std::lock_guard<std::mutex> lock(gAuthServicesMutex);
        if (!gSessionStore) {
            gSessionStore = std::make_shared<SessionAuthStore>();
        }
        return gSessionStore;
    }

    std::shared_ptr<ISupabaseAuthVerifier> AuthServices::verifier() {
        std::lock_guard<std::mutex> lock(gAuthServicesMutex);

        if (gVerifier) {
            return gVerifier;
        }

        if (gProjectUrl.empty()) {
            gProjectUrl = readEnv("SUPABASE_URL");
        }
        if (gPublishableKey.empty()) {
            gPublishableKey = readEnv("SUPABASE_ANON_KEY");
        }
        if (gProjectUrl.empty() || gPublishableKey.empty()) {
            return nullptr;
        }

        gVerifier = std::make_shared<SupabaseAuthVerifier>(gProjectUrl, gPublishableKey);
        return gVerifier;
    }

    void AuthServices::configure(std::string projectUrl, std::string publishableKey) {
        std::lock_guard<std::mutex> lock(gAuthServicesMutex);
        gProjectUrl = std::move(projectUrl);
        gPublishableKey = std::move(publishableKey);
        gVerifier.reset();
    }

    void AuthServices::setAllowDevAuthTokens(bool enabled) {
        std::lock_guard<std::mutex> lock(gAuthServicesMutex);
        gAllowDevAuthTokens = enabled;
    }

    bool AuthServices::allowDevAuthTokens() {
        std::lock_guard<std::mutex> lock(gAuthServicesMutex);
        return gAllowDevAuthTokens;
    }

} // namespace eds::server_new::auth
