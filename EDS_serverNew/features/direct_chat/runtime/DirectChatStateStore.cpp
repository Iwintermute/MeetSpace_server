#include "features/direct_chat/runtime/DirectChatStateStore.h"

namespace eds::server_new::features::direct_chat {
    namespace {

        core::contracts::OperationStatus resolveActorSession(
            const std::shared_ptr<eds::server_new::auth::SessionAuthStore>& sessionStore,
            std::string_view peerId,
            eds::server_new::auth::AuthenticatedSession& actor) {
            if (!sessionStore) {
                return core::contracts::OperationStatus::failure("DirectChatStateStore session store is not configured.");
            }

            const auto handle = sessionStore->resolvePeer(std::string(peerId));
            if (!handle.has_value()) {
                return core::contracts::OperationStatus::failure("direct chat peer is not bound.");
            }

            const auto candidate = sessionStore->get(*handle);
            if (!candidate.has_value() || !candidate->authenticated) {
                return core::contracts::OperationStatus::failure("direct chat session is not authenticated.");
            }

            actor = *candidate;
            return core::contracts::OperationStatus::success();
        }

        bool looksLikeUuid(std::string_view value) {
            if (value.size() != 36) {
                return false;
            }

            for (std::size_t index = 0; index < value.size(); ++index) {
                const auto ch = value[index];
                if (index == 8 || index == 13 || index == 18 || index == 23) {
                    if (ch != '-') {
                        return false;
                    }
                    continue;
                }

                const auto isHex = (ch >= '0' && ch <= '9') ||
                    (ch >= 'a' && ch <= 'f') ||
                    (ch >= 'A' && ch <= 'F');
                if (!isHex) {
                    return false;
                }
            }

            return true;
        }

        core::contracts::OperationStatus resolveTargetUserFromPeer(
            const std::shared_ptr<eds::server_new::auth::SessionAuthStore>& sessionStore,
            std::string_view targetPeerId,
            std::string& resolvedTargetUserId) {
            auto targetHandle = sessionStore->resolvePeer(std::string(targetPeerId));
            if (!targetHandle.has_value()) {
                return core::contracts::OperationStatus::failure("target peer is not connected.");
            }

            auto targetSession = sessionStore->get(*targetHandle);
            if (!targetSession.has_value() || !targetSession->authenticated) {
                return core::contracts::OperationStatus::failure("target peer session is not authenticated.");
            }

            resolvedTargetUserId = targetSession->userId;
            if (resolvedTargetUserId.empty()) {
                return core::contracts::OperationStatus::failure("target peer user is not resolved.");
            }

            return core::contracts::OperationStatus::success();
        }

        core::contracts::OperationStatus resolveTargetUserId(
            const std::shared_ptr<eds::server_new::auth::SessionAuthStore>& sessionStore,
            const DirectChatCommand& command,
            std::string& resolvedTargetUserId) {
            resolvedTargetUserId = command.targetUserId;
            auto resolvedTargetPeerId = command.targetPeerId;
            if (!resolvedTargetUserId.empty() &&
                !looksLikeUuid(resolvedTargetUserId)) {
                if (resolvedTargetPeerId.empty()) {
                    resolvedTargetPeerId = resolvedTargetUserId;
                }
                resolvedTargetUserId.clear();
            }

            if (resolvedTargetUserId.empty() && !resolvedTargetPeerId.empty()) {
                auto targetStatus = resolveTargetUserFromPeer(sessionStore, resolvedTargetPeerId, resolvedTargetUserId);
                if (!targetStatus.ok) {
                    return targetStatus;
                }
            }

            if (resolvedTargetUserId.empty()) {
                return core::contracts::OperationStatus::failure("target user is not resolved.");
            }

            if (!looksLikeUuid(resolvedTargetUserId)) {
                return core::contracts::OperationStatus::failure("target user id is invalid.");
            }

            return core::contracts::OperationStatus::success();
        }

        core::contracts::OperationStatus tryResolveTargetUserId(
            const std::shared_ptr<eds::server_new::auth::SessionAuthStore>& sessionStore,
            const DirectChatCommand& command,
            std::string& resolvedTargetUserId) {
            resolvedTargetUserId = command.targetUserId;
            auto resolvedTargetPeerId = command.targetPeerId;
            if (!resolvedTargetUserId.empty() &&
                !looksLikeUuid(resolvedTargetUserId)) {
                if (resolvedTargetPeerId.empty()) {
                    resolvedTargetPeerId = resolvedTargetUserId;
                }
                resolvedTargetUserId.clear();
            }

            if (resolvedTargetUserId.empty() && !resolvedTargetPeerId.empty()) {
                auto targetStatus = resolveTargetUserFromPeer(sessionStore, resolvedTargetPeerId, resolvedTargetUserId);
                if (!targetStatus.ok) {
                    if (command.threadId.empty()) {
                        return targetStatus;
                    }
                    resolvedTargetUserId.clear();
                    return core::contracts::OperationStatus::success();
                }
            }


            return core::contracts::OperationStatus::success();
        }

    } // namespace

    DirectChatStateStore::DirectChatStateStore(
        std::shared_ptr<eds::server_new::auth::SessionAuthStore> sessionStore,
        std::shared_ptr<eds::server_new::infrastructure::db::MessengerRepository> repository)
        : sessionStore_(std::move(sessionStore)),
        repository_(std::move(repository)) {
    }

    core::contracts::OperationStatus DirectChatStateStore::sendMessage(const DirectChatCommand& command) {
        if (!repository_ || !repository_->isReady()) {
            return core::contracts::OperationStatus::failure("DirectChatStateStore repository is not configured.");
        }

        eds::server_new::auth::AuthenticatedSession actor;
        auto actorStatus = resolveActorSession(sessionStore_, command.peerId, actor);
        if (!actorStatus.ok) {
            return actorStatus;
        }

        std::string resolvedTargetUserId;
        auto resolveStatus = resolveTargetUserId(sessionStore_, command, resolvedTargetUserId);
        if (!resolveStatus.ok) {
            return resolveStatus;
        }

        return repository_->sendDirectMessage(
            actor.userId,
            actor.peerId,
            resolvedTargetUserId,
            command.clientRequestId,
            command.text);
    }

    core::contracts::OperationStatus DirectChatStateStore::listThreads(const DirectChatCommand& command) {
        if (!repository_ || !repository_->isReady()) {
            return core::contracts::OperationStatus::failure("DirectChatStateStore repository is not configured.");
        }

        eds::server_new::auth::AuthenticatedSession actor;
        auto actorStatus = resolveActorSession(sessionStore_, command.peerId, actor);
        if (!actorStatus.ok) {
            return actorStatus;
        }

        return repository_->listDirectThreads(actor.userId, command.limit);
    }

    core::contracts::OperationStatus DirectChatStateStore::syncMessages(const DirectChatCommand& command) {
        if (!repository_ || !repository_->isReady()) {
            return core::contracts::OperationStatus::failure("DirectChatStateStore repository is not configured.");
        }

        eds::server_new::auth::AuthenticatedSession actor;
        auto actorStatus = resolveActorSession(sessionStore_, command.peerId, actor);
        if (!actorStatus.ok) {
            return actorStatus;
        }

        std::string resolvedTargetUserId;
        auto resolveStatus = tryResolveTargetUserId(sessionStore_, command, resolvedTargetUserId);
        if (!resolveStatus.ok) {
            return resolveStatus;
        }

        return repository_->listDirectMessages(
            actor.userId,
            resolvedTargetUserId,
            command.threadId,
            command.limit,
            command.beforeCreatedAt,
            command.afterCreatedAt);
    }

    core::contracts::OperationStatus DirectChatStateStore::ackMessages(const DirectChatCommand& command) {
        if (!repository_ || !repository_->isReady()) {
            return core::contracts::OperationStatus::failure("DirectChatStateStore repository is not configured.");
        }

        eds::server_new::auth::AuthenticatedSession actor;
        auto actorStatus = resolveActorSession(sessionStore_, command.peerId, actor);
        if (!actorStatus.ok) {
            return actorStatus;
        }

        std::string resolvedTargetUserId;
        auto resolveStatus = tryResolveTargetUserId(sessionStore_, command, resolvedTargetUserId);
        if (!resolveStatus.ok) {
            return resolveStatus;
        }

        return repository_->ackDirectMessages(
            actor.userId,
            resolvedTargetUserId,
            command.threadId,
            command.messageIds,
            command.markRead);
    }

    core::contracts::OperationStatus DirectChatStateStore::searchUsers(const DirectChatCommand& command) {
        if (!repository_ || !repository_->isReady()) {
            return core::contracts::OperationStatus::failure("DirectChatStateStore repository is not configured.");
        }

        eds::server_new::auth::AuthenticatedSession actor;
        auto actorStatus = resolveActorSession(sessionStore_, command.peerId, actor);
        if (!actorStatus.ok) {
            return actorStatus;
        }

        return repository_->searchUsersByEmail(actor.userId, command.query, command.limit);
    }

} // namespace eds::server_new::features::direct_chat
