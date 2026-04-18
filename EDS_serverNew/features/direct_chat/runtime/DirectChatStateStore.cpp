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

        auto resolvedTargetUserId = command.targetUserId;
        if (resolvedTargetUserId.empty() && !command.targetPeerId.empty()) {
            auto targetHandle = sessionStore_->resolvePeer(command.targetPeerId);
            if (!targetHandle.has_value()) {
                return core::contracts::OperationStatus::failure("target peer is not connected.");
            }

            auto targetSession = sessionStore_->get(*targetHandle);
            if (!targetSession.has_value() || !targetSession->authenticated) {
                return core::contracts::OperationStatus::failure("target peer session is not authenticated.");
            }

            resolvedTargetUserId = targetSession->userId;
            if (resolvedTargetUserId.empty()) {
                return core::contracts::OperationStatus::failure("target peer user is not resolved.");
            }
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

        auto resolvedTargetUserId = command.targetUserId;
        if (resolvedTargetUserId.empty() && !command.targetPeerId.empty()) {
            auto targetHandle = sessionStore_->resolvePeer(command.targetPeerId);
            if (targetHandle.has_value()) {
                auto targetSession = sessionStore_->get(*targetHandle);
                if (targetSession.has_value() && targetSession->authenticated) {
                    resolvedTargetUserId = targetSession->userId;
                }
            }
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

        auto resolvedTargetUserId = command.targetUserId;
        if (resolvedTargetUserId.empty() && !command.targetPeerId.empty()) {
            auto targetHandle = sessionStore_->resolvePeer(command.targetPeerId);
            if (targetHandle.has_value()) {
                auto targetSession = sessionStore_->get(*targetHandle);
                if (targetSession.has_value() && targetSession->authenticated) {
                    resolvedTargetUserId = targetSession->userId;
                }
            }
        }

        return repository_->ackDirectMessages(
            actor.userId,
            resolvedTargetUserId,
            command.threadId,
            command.messageIds,
            command.markRead);
    }

} // namespace eds::server_new::features::direct_chat