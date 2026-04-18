#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace eds::server_new::auth {

    struct AuthenticatedSession {
        std::uintptr_t sessionHandle = 0;
        std::string peerId;
        std::string userId;
        std::string email;
        std::string accessToken;
        std::string deviceId;
        std::string dbSessionId;
        std::string dbConnectionId;
        bool authenticated = false;
    };

    class SessionAuthStore {
    public:
        SessionAuthStore() = default;
        ~SessionAuthStore() = default;

        SessionAuthStore(const SessionAuthStore&) = delete;
        SessionAuthStore& operator=(const SessionAuthStore&) = delete;

        void bind(AuthenticatedSession session);
        void unbind(std::uintptr_t sessionHandle);
        void unbindPeer(const std::string& peerId);

        std::optional<AuthenticatedSession> get(std::uintptr_t sessionHandle) const;
        std::optional<std::uintptr_t> resolvePeer(const std::string& peerId) const;

        std::vector<std::string> resolvePeersForUser(const std::string& userId) const;
        std::vector<AuthenticatedSession> listSessionsForUser(const std::string& userId) const;

    private:
        void removeIndexesNoLock(const AuthenticatedSession& session);

    private:
        mutable std::mutex mutex_;
        std::unordered_map<std::uintptr_t, AuthenticatedSession> byHandle_;
        std::unordered_map<std::string, std::uintptr_t> peerToHandle_;
        std::unordered_map<std::string, std::unordered_set<std::uintptr_t>> userToHandles_;
        std::unordered_map<std::string, std::unordered_set<std::string>> userToPeers_;
    };

} // namespace eds::server_new::auth