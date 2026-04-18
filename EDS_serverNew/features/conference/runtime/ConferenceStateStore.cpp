#include "features/conference/runtime/ConferenceStateStore.h"

namespace eds::server_new::features::conference {

    ConferenceStateStore::ConferenceStateStore(
        std::shared_ptr<eds::server_new::auth::SessionAuthStore> sessionStore,
        std::shared_ptr<eds::server_new::infrastructure::db::MessengerRepository> repository)
        : sessionStore_(std::move(sessionStore)),
        repository_(std::move(repository)) {
    }

    core::contracts::OperationStatus ConferenceStateStore::createConference(const ConferenceCommand& command) {
        if (!sessionStore_) {
            return core::contracts::OperationStatus::failure("ConferenceStateStore session store is not configured.");
        }
        if (!repository_ || !repository_->isReady()) {
            return core::contracts::OperationStatus::failure("ConferenceStateStore repository is not configured.");
        }

        auto handle = sessionStore_->resolvePeer(command.peerId);
        if (!handle.has_value()) {
            return core::contracts::OperationStatus::failure("conference peer is not bound.");
        }

        auto actor = sessionStore_->get(*handle);
        if (!actor.has_value() || !actor->authenticated) {
            return core::contracts::OperationStatus::failure("conference session is not authenticated.");
        }

        return repository_->createConference(
            actor->userId,
            command.conferenceId,
            actor->peerId,
            actor->sessionHandle);
    }

    core::contracts::OperationStatus ConferenceStateStore::getConference(const ConferenceCommand& command) {
        if (!sessionStore_) {
            return core::contracts::OperationStatus::failure("ConferenceStateStore session store is not configured.");
        }
        if (!repository_ || !repository_->isReady()) {
            return core::contracts::OperationStatus::failure("ConferenceStateStore repository is not configured.");
        }

        auto handle = sessionStore_->resolvePeer(command.peerId);
        if (!handle.has_value()) {
            return core::contracts::OperationStatus::failure("conference peer is not bound.");
        }

        auto actor = sessionStore_->get(*handle);
        if (!actor.has_value() || !actor->authenticated) {
            return core::contracts::OperationStatus::failure("conference session is not authenticated.");
        }

        return repository_->getConference(actor->userId, command.conferenceId);
    }

    core::contracts::OperationStatus ConferenceStateStore::closeConference(const ConferenceCommand& command) {
        if (!sessionStore_) {
            return core::contracts::OperationStatus::failure("ConferenceStateStore session store is not configured.");
        }
        if (!repository_ || !repository_->isReady()) {
            return core::contracts::OperationStatus::failure("ConferenceStateStore repository is not configured.");
        }

        auto handle = sessionStore_->resolvePeer(command.peerId);
        if (!handle.has_value()) {
            return core::contracts::OperationStatus::failure("conference peer is not bound.");
        }

        auto actor = sessionStore_->get(*handle);
        if (!actor.has_value() || !actor->authenticated) {
            return core::contracts::OperationStatus::failure("conference session is not authenticated.");
        }

        return repository_->closeConference(actor->userId, command.conferenceId);
    }

    core::contracts::OperationStatus ConferenceStateStore::joinConference(const ConferenceCommand& command) {
        if (!sessionStore_) {
            return core::contracts::OperationStatus::failure("ConferenceStateStore session store is not configured.");
        }
        if (!repository_ || !repository_->isReady()) {
            return core::contracts::OperationStatus::failure("ConferenceStateStore repository is not configured.");
        }

        auto handle = sessionStore_->resolvePeer(command.peerId);
        if (!handle.has_value()) {
            return core::contracts::OperationStatus::failure("conference peer is not bound.");
        }

        auto actor = sessionStore_->get(*handle);
        if (!actor.has_value() || !actor->authenticated) {
            return core::contracts::OperationStatus::failure("conference session is not authenticated.");
        }

        return repository_->joinConference(
            actor->userId,
            command.conferenceId,
            actor->peerId,
            actor->sessionHandle);
    }

    core::contracts::OperationStatus ConferenceStateStore::leaveConference(const ConferenceCommand& command) {
        if (!sessionStore_) {
            return core::contracts::OperationStatus::failure("ConferenceStateStore session store is not configured.");
        }
        if (!repository_ || !repository_->isReady()) {
            return core::contracts::OperationStatus::failure("ConferenceStateStore repository is not configured.");
        }

        auto handle = sessionStore_->resolvePeer(command.peerId);
        if (!handle.has_value()) {
            return core::contracts::OperationStatus::failure("conference peer is not bound.");
        }

        auto actor = sessionStore_->get(*handle);
        if (!actor.has_value() || !actor->authenticated) {
            return core::contracts::OperationStatus::failure("conference session is not authenticated.");
        }

        return repository_->leaveConference(
            actor->userId,
            command.conferenceId,
            actor->peerId,
            actor->sessionHandle);
    }

    core::contracts::OperationStatus ConferenceStateStore::listMembers(const ConferenceCommand& command) {
        if (!sessionStore_) {
            return core::contracts::OperationStatus::failure("ConferenceStateStore session store is not configured.");
        }
        if (!repository_ || !repository_->isReady()) {
            return core::contracts::OperationStatus::failure("ConferenceStateStore repository is not configured.");
        }

        auto handle = sessionStore_->resolvePeer(command.peerId);
        if (!handle.has_value()) {
            return core::contracts::OperationStatus::failure("conference peer is not bound.");
        }

        auto actor = sessionStore_->get(*handle);
        if (!actor.has_value() || !actor->authenticated) {
            return core::contracts::OperationStatus::failure("conference session is not authenticated.");
        }

        return repository_->listConferenceMembers(actor->userId, command.conferenceId);
    }

} // namespace eds::server_new::features::conference