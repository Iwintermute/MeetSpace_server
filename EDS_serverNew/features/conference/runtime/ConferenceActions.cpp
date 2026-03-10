#include "features/conference/runtime/ConferenceActions.h"

#include <utility>

namespace eds::server_new::features::conference {

ConferenceActionBase::ConferenceActionBase(std::string name, std::shared_ptr<ConferenceStateStore> stateStore)
    : BaseModule(std::move(name), static_cast<ModuleId>(-1)),
      stateStore_(std::move(stateStore)) {
}

core::contracts::OperationStatus ConferenceActionBase::readCommand(
    const core::contracts::IMessage& message,
    const core::contracts::TypedMessage<ConferenceCommand>*& typedMessage) const {
    typedMessage = dynamic_cast<const core::contracts::TypedMessage<ConferenceCommand>*>(&message);
    if (!typedMessage) {
        return core::contracts::OperationStatus::failure("Conference action expects TypedMessage<ConferenceCommand> payload.");
    }

    return core::contracts::OperationStatus::success();
}

bool ConferenceActionBase::onInitialize() {
    return true;
}

void ConferenceActionBase::onShutdown() {
}

CreateConferenceAction::CreateConferenceAction(std::shared_ptr<ConferenceStateStore> stateStore)
    : ConferenceActionBase("CreateConferenceAction", std::move(stateStore)) {
}

core::contracts::OperationStatus CreateConferenceAction::execute(const core::contracts::IMessage& message) {
    if (!stateStore_) {
        return core::contracts::OperationStatus::failure("Conference state store is not configured.");
    }

    const core::contracts::TypedMessage<ConferenceCommand>* typedMessage = nullptr;
    const auto readStatus = readCommand(message, typedMessage);
    if (!readStatus.ok) {
        return readStatus;
    }
    return stateStore_->createConference(typedMessage->payload());
}

GetConferenceAction::GetConferenceAction(std::shared_ptr<ConferenceStateStore> stateStore)
    : ConferenceActionBase("GetConferenceAction", std::move(stateStore)) {
}

core::contracts::OperationStatus GetConferenceAction::execute(const core::contracts::IMessage& message) {
    if (!stateStore_) {
        return core::contracts::OperationStatus::failure("Conference state store is not configured.");
    }

    const core::contracts::TypedMessage<ConferenceCommand>* typedMessage = nullptr;
    const auto readStatus = readCommand(message, typedMessage);
    if (!readStatus.ok) {
        return readStatus;
    }
    return stateStore_->getConference(typedMessage->payload());
}

CloseConferenceAction::CloseConferenceAction(std::shared_ptr<ConferenceStateStore> stateStore)
    : ConferenceActionBase("CloseConferenceAction", std::move(stateStore)) {
}

core::contracts::OperationStatus CloseConferenceAction::execute(const core::contracts::IMessage& message) {
    if (!stateStore_) {
        return core::contracts::OperationStatus::failure("Conference state store is not configured.");
    }

    const core::contracts::TypedMessage<ConferenceCommand>* typedMessage = nullptr;
    const auto readStatus = readCommand(message, typedMessage);
    if (!readStatus.ok) {
        return readStatus;
    }
    return stateStore_->closeConference(typedMessage->payload());
}

JoinConferenceAction::JoinConferenceAction(std::shared_ptr<ConferenceStateStore> stateStore)
    : ConferenceActionBase("JoinConferenceAction", std::move(stateStore)) {
}

core::contracts::OperationStatus JoinConferenceAction::execute(const core::contracts::IMessage& message) {
    if (!stateStore_) {
        return core::contracts::OperationStatus::failure("Conference state store is not configured.");
    }

    const core::contracts::TypedMessage<ConferenceCommand>* typedMessage = nullptr;
    const auto readStatus = readCommand(message, typedMessage);
    if (!readStatus.ok) {
        return readStatus;
    }
    return stateStore_->joinConference(typedMessage->payload());
}

LeaveConferenceAction::LeaveConferenceAction(std::shared_ptr<ConferenceStateStore> stateStore)
    : ConferenceActionBase("LeaveConferenceAction", std::move(stateStore)) {
}

core::contracts::OperationStatus LeaveConferenceAction::execute(const core::contracts::IMessage& message) {
    if (!stateStore_) {
        return core::contracts::OperationStatus::failure("Conference state store is not configured.");
    }

    const core::contracts::TypedMessage<ConferenceCommand>* typedMessage = nullptr;
    const auto readStatus = readCommand(message, typedMessage);
    if (!readStatus.ok) {
        return readStatus;
    }
    return stateStore_->leaveConference(typedMessage->payload());
}

ListMembersAction::ListMembersAction(std::shared_ptr<ConferenceStateStore> stateStore)
    : ConferenceActionBase("ListMembersAction", std::move(stateStore)) {
}

core::contracts::OperationStatus ListMembersAction::execute(const core::contracts::IMessage& message) {
    if (!stateStore_) {
        return core::contracts::OperationStatus::failure("Conference state store is not configured.");
    }

    const core::contracts::TypedMessage<ConferenceCommand>* typedMessage = nullptr;
    const auto readStatus = readCommand(message, typedMessage);
    if (!readStatus.ok) {
        return readStatus;
    }
    return stateStore_->listMembers(typedMessage->payload());
}

} // namespace eds::server_new::features::conference
