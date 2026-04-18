#include "features/chat/runtime/ChatStateStore.h"

namespace eds::server_new::features::chat {
    namespace {

        core::contracts::OperationStatus resolveActorSession(
            const std::shared_ptr<eds::server_new::auth::SessionAuthStore>& sessionStore,
            std::string_view peerId,
            eds::server_new::auth::AuthenticatedSession& actor) {
            if (!sessionStore) {
                return core::contracts::OperationStatus::failure("ChatStateStore session store is not configured.");
            }

            const auto handle = sessionStore->resolvePeer(std::string(peerId));
            if (!handle.has_value()) {
                return core::contracts::OperationStatus::failure("conference chat peer is not bound.");
            }

            const auto candidate = sessionStore->get(*handle);
            if (!candidate.has_value() || !candidate->authenticated) {
                return core::contracts::OperationStatus::failure("conference chat session is not authenticated.");
            }

            actor = *candidate;
            return core::contracts::OperationStatus::success();
        }

    } // namespace

    ChatStateStore::ChatStateStore(
        std::shared_ptr<eds::server_new::auth::SessionAuthStore> sessionStore,
        std::shared_ptr<eds::server_new::infrastructure::db::MessengerRepository> repository)
        : sessionStore_(std::move(sessionStore)),
        repository_(std::move(repository)) {
    }

    core::contracts::OperationStatus ChatStateStore::sendMessage(const ChatCommand& command) {
        if (!repository_ || !repository_->isReady()) {
            return core::contracts::OperationStatus::failure("ChatStateStore repository is not configured.");
        }
        eds::server_new::auth::AuthenticatedSession actor;
        auto actorStatus = resolveActorSession(sessionStore_, command.peerId, actor);
        if (!actorStatus.ok) {
            return actorStatus;
            return core::contracts::OperationStatus::failure("conference chat session is not authenticated.");
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

        return repository_->sendConferenceMessage(
            actor.userId,
            actor.peerId,
            command.conferenceId,
            resolvedTargetUserId,
            command.clientRequestId,
            command.text);
    }

    core::contracts::OperationStatus ChatStateStore::syncMessages(const ChatCommand& command) {
        if (!repository_ || !repository_->isReady()) {
            return core::contracts::OperationStatus::failure("ChatStateStore repository is not configured.");
        }

        eds::server_new::auth::AuthenticatedSession actor;
        auto actorStatus = resolveActorSession(sessionStore_, command.peerId, actor);
        if (!actorStatus.ok) {
            return actorStatus;
        }

        return repository_->listConferenceMessages(
            actor.userId,
            command.conferenceId,
            command.limit,
            command.beforeCreatedAt,
            command.afterCreatedAt);
    }

    core::contracts::OperationStatus ChatStateStore::ackMessages(const ChatCommand& command) {
        if (!repository_ || !repository_->isReady()) {
            return core::contracts::OperationStatus::failure("ChatStateStore repository is not configured.");
        }

        eds::server_new::auth::AuthenticatedSession actor;
        auto actorStatus = resolveActorSession(sessionStore_, command.peerId, actor);
        if (!actorStatus.ok) {
            return actorStatus;
        }

        return repository_->ackConferenceMessages(
            actor.userId,
            command.conferenceId,
            command.messageIds,
            command.markRead);
    }

} // namespace eds::server_new::features::chat