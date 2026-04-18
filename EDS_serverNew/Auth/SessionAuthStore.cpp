#include "Auth/SessionAuthStore.h"

namespace eds::server_new::auth {

    void SessionAuthStore::bind(AuthenticatedSession session) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto existing = byHandle_.find(session.sessionHandle);
        if (existing != byHandle_.end()) {
            removeIndexesNoLock(existing->second);
            byHandle_.erase(existing);
        }

        auto peerExisting = peerToHandle_.find(session.peerId);
        if (peerExisting != peerToHandle_.end()) {
            auto byHandleIt = byHandle_.find(peerExisting->second);
            if (byHandleIt != byHandle_.end()) {
                removeIndexesNoLock(byHandleIt->second);
                byHandle_.erase(byHandleIt);
            }
            else {
                peerToHandle_.erase(peerExisting);
            }
        }

        byHandle_[session.sessionHandle] = session;
        peerToHandle_[session.peerId] = session.sessionHandle;

        if (!session.userId.empty()) {
            userToHandles_[session.userId].insert(session.sessionHandle);
            userToPeers_[session.userId].insert(session.peerId);
        }
    }

    void SessionAuthStore::unbind(std::uintptr_t sessionHandle) {
        std::lock_guard<std::mutex> lock(mutex_);

        const auto it = byHandle_.find(sessionHandle);
        if (it == byHandle_.end()) {
            return;
        }

        removeIndexesNoLock(it->second);
        byHandle_.erase(it);
    }

    void SessionAuthStore::unbindPeer(const std::string& peerId) {
        if (peerId.empty()) {
            return;
        }

        std::lock_guard<std::mutex> lock(mutex_);

        const auto pit = peerToHandle_.find(peerId);
        if (pit == peerToHandle_.end()) {
            return;
        }

        const auto hit = byHandle_.find(pit->second);
        if (hit == byHandle_.end()) {
            peerToHandle_.erase(pit);
            return;
        }

        removeIndexesNoLock(hit->second);
        byHandle_.erase(hit);
    }

    std::optional<AuthenticatedSession> SessionAuthStore::get(std::uintptr_t sessionHandle) const {
        std::lock_guard<std::mutex> lock(mutex_);

        const auto it = byHandle_.find(sessionHandle);
        if (it == byHandle_.end()) {
            return std::nullopt;
        }

        return it->second;
    }

    std::optional<std::uintptr_t> SessionAuthStore::resolvePeer(const std::string& peerId) const {
        std::lock_guard<std::mutex> lock(mutex_);

        const auto it = peerToHandle_.find(peerId);
        if (it == peerToHandle_.end()) {
            return std::nullopt;
        }

        return it->second;
    }

    std::vector<std::string> SessionAuthStore::resolvePeersForUser(const std::string& userId) const {
        std::lock_guard<std::mutex> lock(mutex_);

        std::vector<std::string> peers;
        const auto it = userToPeers_.find(userId);
        if (it == userToPeers_.end()) {
            return peers;
        }

        peers.reserve(it->second.size());
        for (const auto& peer : it->second) {
            peers.push_back(peer);
        }

        return peers;
    }

    std::vector<AuthenticatedSession> SessionAuthStore::listSessionsForUser(const std::string& userId) const {
        std::lock_guard<std::mutex> lock(mutex_);

        std::vector<AuthenticatedSession> result;
        const auto it = userToHandles_.find(userId);
        if (it == userToHandles_.end()) {
            return result;
        }

        result.reserve(it->second.size());
        for (const auto handle : it->second) {
            const auto sit = byHandle_.find(handle);
            if (sit != byHandle_.end()) {
                result.push_back(sit->second);
            }
        }

        return result;
    }

    void SessionAuthStore::removeIndexesNoLock(const AuthenticatedSession& session) {
        peerToHandle_.erase(session.peerId);

        if (!session.userId.empty()) {
            auto hit = userToHandles_.find(session.userId);
            if (hit != userToHandles_.end()) {
                hit->second.erase(session.sessionHandle);
                if (hit->second.empty()) {
                    userToHandles_.erase(hit);
                }
            }

            auto pit = userToPeers_.find(session.userId);
            if (pit != userToPeers_.end()) {
                pit->second.erase(session.peerId);
                if (pit->second.empty()) {
                    userToPeers_.erase(pit);
                }
            }
        }
    }

} // namespace eds::server_new::auth