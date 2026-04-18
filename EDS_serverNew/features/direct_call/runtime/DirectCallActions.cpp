#include "features/direct_call/runtime/DirectCallActions.h"

#include <utility>

namespace eds::server_new::features::direct_call {

    DirectCallActionBase::DirectCallActionBase(std::string name, std::shared_ptr<DirectCallStateStore> stateStore)
        : BaseModule(std::move(name), static_cast<ModuleId>(-1)),
        stateStore_(std::move(stateStore)) {
    }

    core::contracts::OperationStatus DirectCallActionBase::readCommand(
        const core::contracts::IMessage& message,
        const core::contracts::TypedMessage<DirectCallCommand>*& typedMessage) const {
        typedMessage = dynamic_cast<const core::contracts::TypedMessage<DirectCallCommand>*>(&message);
        if (!typedMessage) {
            return core::contracts::OperationStatus::failure(
                "Direct call action expects TypedMessage<DirectCallCommand> payload.");
        }
        return core::contracts::OperationStatus::success();
    }

    bool DirectCallActionBase::onInitialize() {
        return true;
    }

    void DirectCallActionBase::onShutdown() {
    }

    CreateDirectCallAction::CreateDirectCallAction(std::shared_ptr<DirectCallStateStore> stateStore)
        : DirectCallActionBase("CreateDirectCallAction", std::move(stateStore)) {
    }

    core::contracts::OperationStatus CreateDirectCallAction::execute(const core::contracts::IMessage& message) {
        if (!stateStore_) {
            return core::contracts::OperationStatus::failure("Direct call state store is not configured.");
        }

        const core::contracts::TypedMessage<DirectCallCommand>* typedMessage = nullptr;
        const auto readStatus = readCommand(message, typedMessage);
        if (!readStatus.ok) {
            return readStatus;
        }
        return stateStore_->createCall(typedMessage->payload());
    }

    AcceptDirectCallAction::AcceptDirectCallAction(std::shared_ptr<DirectCallStateStore> stateStore)
        : DirectCallActionBase("AcceptDirectCallAction", std::move(stateStore)) {
    }

    core::contracts::OperationStatus AcceptDirectCallAction::execute(const core::contracts::IMessage& message) {
        if (!stateStore_) {
            return core::contracts::OperationStatus::failure("Direct call state store is not configured.");
        }

        const core::contracts::TypedMessage<DirectCallCommand>* typedMessage = nullptr;
        const auto readStatus = readCommand(message, typedMessage);
        if (!readStatus.ok) {
            return readStatus;
        }
        return stateStore_->acceptCall(typedMessage->payload());
    }

    DeclineDirectCallAction::DeclineDirectCallAction(std::shared_ptr<DirectCallStateStore> stateStore)
        : DirectCallActionBase("DeclineDirectCallAction", std::move(stateStore)) {
    }

    core::contracts::OperationStatus DeclineDirectCallAction::execute(const core::contracts::IMessage& message) {
        if (!stateStore_) {
            return core::contracts::OperationStatus::failure("Direct call state store is not configured.");
        }

        const core::contracts::TypedMessage<DirectCallCommand>* typedMessage = nullptr;
        const auto readStatus = readCommand(message, typedMessage);
        if (!readStatus.ok) {
            return readStatus;
        }
        return stateStore_->declineCall(typedMessage->payload());
    }

    HangupDirectCallAction::HangupDirectCallAction(std::shared_ptr<DirectCallStateStore> stateStore)
        : DirectCallActionBase("HangupDirectCallAction", std::move(stateStore)) {
    }

    core::contracts::OperationStatus HangupDirectCallAction::execute(const core::contracts::IMessage& message) {
        if (!stateStore_) {
            return core::contracts::OperationStatus::failure("Direct call state store is not configured.");
        }

        const core::contracts::TypedMessage<DirectCallCommand>* typedMessage = nullptr;
        const auto readStatus = readCommand(message, typedMessage);
        if (!readStatus.ok) {
            return readStatus;
        }
        return stateStore_->hangupCall(typedMessage->payload());
    }

} // namespace eds::server_new::features::direct_call