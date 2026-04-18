#include "features/direct_call/runtime/DirectCallStateStore.h"

namespace eds::server_new::features::direct_call {

    DirectCallStateStore::DirectCallStateStore(
        std::shared_ptr<eds::server_new::auth::SessionAuthStore> sessionStore,
        std::shared_ptr<eds::server_new::infrastructure::db::MessengerRepository> repository)
        : sessionStore_(std::move(sessionStore)),
        repository_(std::move(repository)) {
    }

    core::contracts::OperationStatus DirectCallStateStore::createCall(const DirectCallCommand& command) {
        if (!sessionStore_) {
            return core::contracts::OperationStatus::failure("DirectCallStateStore session store is not configured.");
        }
        if (!repository_ || !repository_->isReady()) {
            return core::contracts::OperationStatus::failure("DirectCallStateStore repository is not configured.");
        }

        auto handle = sessionStore_->resolvePeer(command.peerId);
        if (!handle.has_value()) {
            return core::contracts::OperationStatus::failure("direct call peer is not bound.");
        }

        auto actor = sessionStore_->get(*handle);
        if (!actor.has_value() || !actor->authenticated) {
            return core::contracts::OperationStatus::failure("direct call session is not authenticated.");
        }

        return repository_->createDirectCall(
            actor->userId,
            actor->peerId,
            command.targetUserId,
            command.clientRequestId);
    }

    core::contracts::OperationStatus DirectCallStateStore::acceptCall(const DirectCallCommand& command) {
        if (!sessionStore_) {
            return core::contracts::OperationStatus::failure("DirectCallStateStore session store is not configured.");
        }
        if (!repository_ || !repository_->isReady()) {
            return core::contracts::OperationStatus::failure("DirectCallStateStore repository is not configured.");
        }

        auto handle = sessionStore_->resolvePeer(command.peerId);
        if (!handle.has_value()) {
            return core::contracts::OperationStatus::failure("direct call peer is not bound.");
        }

        auto actor = sessionStore_->get(*handle);
        if (!actor.has_value() || !actor->authenticated) {
            return core::contracts::OperationStatus::failure("direct call session is not authenticated.");
        }

        return repository_->acceptDirectCall(
            actor->userId,
            actor->peerId,
            command.callId);
    }

    core::contracts::OperationStatus DirectCallStateStore::declineCall(const DirectCallCommand& command) {
        if (!sessionStore_) {
            return core::contracts::OperationStatus::failure("DirectCallStateStore session store is not configured.");
        }
        if (!repository_ || !repository_->isReady()) {
            return core::contracts::OperationStatus::failure("DirectCallStateStore repository is not configured.");
        }

        auto handle = sessionStore_->resolvePeer(command.peerId);
        if (!handle.has_value()) {
            return core::contracts::OperationStatus::failure("direct call peer is not bound.");
        }

        auto actor = sessionStore_->get(*handle);
        if (!actor.has_value() || !actor->authenticated) {
            return core::contracts::OperationStatus::failure("direct call session is not authenticated.");
        }

        return repository_->declineDirectCall(
            actor->userId,
            actor->peerId,
            command.callId);
    }

    core::contracts::OperationStatus DirectCallStateStore::hangupCall(const DirectCallCommand& command) {
        if (!sessionStore_) {
            return core::contracts::OperationStatus::failure("DirectCallStateStore session store is not configured.");
        }
        if (!repository_ || !repository_->isReady()) {
            return core::contracts::OperationStatus::failure("DirectCallStateStore repository is not configured.");
        }

        auto handle = sessionStore_->resolvePeer(command.peerId);
        if (!handle.has_value()) {
            return core::contracts::OperationStatus::failure("direct call peer is not bound.");
        }

        auto actor = sessionStore_->get(*handle);
        if (!actor.has_value() || !actor->authenticated) {
            return core::contracts::OperationStatus::failure("direct call session is not authenticated.");
        }

        return repository_->hangupDirectCall(
            actor->userId,
            actor->peerId,
            command.callId);
    }

} // namespace eds::server_new::features::direct_call