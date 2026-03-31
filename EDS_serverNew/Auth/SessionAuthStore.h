#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <mutex>
#include <optional>

namespace eds::server_new::auth {

    struct AuthenticatedSession {
        std::uintptr_t sessionHandle = 0;
        std::string peerId;
        std::string userId;
        std::string email;
        std::string accessToken;
        std::string deviceId;
        bool authenticated = false;
    };

    class SessionAuthStore {
    public:
        void bind(AuthenticatedSession session) {
            std::lock_guard<std::mutex> lock(mutex_);
            byHandle_[session.sessionHandle] = session;
            peerToHandle_[session.peerId] = session.sessionHandle;
        }

        void unbind(std::uintptr_t sessionHandle) {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = byHandle_.find(sessionHandle);
            if (it == byHandle_.end()) {
                return;
            }
            peerToHandle_.erase(it->second.peerId);
            byHandle_.erase(it);
        }

        std::optional<AuthenticatedSession> get(std::uintptr_t sessionHandle) const {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = byHandle_.find(sessionHandle);
            if (it == byHandle_.end()) {
                return std::nullopt;
            }
            return it->second;
        }

        std::optional<std::uintptr_t> resolvePeer(const std::string& peerId) const {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = peerToHandle_.find(peerId);
            if (it == peerToHandle_.end()) {
                return std::nullopt;
            }
            return it->second;
        }

    private:
        mutable std::mutex mutex_;
        std::unordered_map<std::uintptr_t, AuthenticatedSession> byHandle_;
        std::unordered_map<std::string, std::uintptr_t> peerToHandle_;
    };

} // namespace eds::server_new::auth