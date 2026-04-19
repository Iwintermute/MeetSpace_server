#include "features/direct_chat/runtime/DirectChatActions.h"

#include <utility>

namespace eds::server_new::features::direct_chat {

    DirectChatActionBase::DirectChatActionBase(std::string name, std::shared_ptr<DirectChatStateStore> stateStore)
        : BaseModule(std::move(name), static_cast<ModuleId>(-1)),
        stateStore_(std::move(stateStore)) {
    }

    core::contracts::OperationStatus DirectChatActionBase::readCommand(
        const core::contracts::IMessage& message,
        const core::contracts::TypedMessage<DirectChatCommand>*& typedMessage) const {
        typedMessage = dynamic_cast<const core::contracts::TypedMessage<DirectChatCommand>*>(&message);
        if (!typedMessage) {
            return core::contracts::OperationStatus::failure(
                "Direct chat action expects TypedMessage<DirectChatCommand> payload.");
        }
        return core::contracts::OperationStatus::success();
    }

    bool DirectChatActionBase::onInitialize() {
        return true;
    }

    void DirectChatActionBase::onShutdown() {
    }

    SendDirectMessageAction::SendDirectMessageAction(std::shared_ptr<DirectChatStateStore> stateStore)
        : DirectChatActionBase("SendDirectMessageAction", std::move(stateStore)) {
    }

    core::contracts::OperationStatus SendDirectMessageAction::execute(const core::contracts::IMessage& message) {
        if (!stateStore_) {
            return core::contracts::OperationStatus::failure("Direct chat state store is not configured.");
        }

        const core::contracts::TypedMessage<DirectChatCommand>* typedMessage = nullptr;
        const auto readStatus = readCommand(message, typedMessage);
        if (!readStatus.ok) {
            return readStatus;
        }
        return stateStore_->sendMessage(typedMessage->payload());
    }

    ListDirectThreadsAction::ListDirectThreadsAction(std::shared_ptr<DirectChatStateStore> stateStore)
        : DirectChatActionBase("ListDirectThreadsAction", std::move(stateStore)) {
    }

    core::contracts::OperationStatus ListDirectThreadsAction::execute(const core::contracts::IMessage& message) {
        if (!stateStore_) {
            return core::contracts::OperationStatus::failure("Direct chat state store is not configured.");
        }

        const core::contracts::TypedMessage<DirectChatCommand>* typedMessage = nullptr;
        const auto readStatus = readCommand(message, typedMessage);
        if (!readStatus.ok) {
            return readStatus;
        }
        return stateStore_->listThreads(typedMessage->payload());
    }

    SyncDirectMessagesAction::SyncDirectMessagesAction(std::shared_ptr<DirectChatStateStore> stateStore)
        : DirectChatActionBase("SyncDirectMessagesAction", std::move(stateStore)) {
    }

    core::contracts::OperationStatus SyncDirectMessagesAction::execute(const core::contracts::IMessage& message) {
        if (!stateStore_) {
            return core::contracts::OperationStatus::failure("Direct chat state store is not configured.");
        }

        const core::contracts::TypedMessage<DirectChatCommand>* typedMessage = nullptr;
        const auto readStatus = readCommand(message, typedMessage);
        if (!readStatus.ok) {
            return readStatus;
        }
        return stateStore_->syncMessages(typedMessage->payload());
    }

    AckDirectMessagesAction::AckDirectMessagesAction(std::shared_ptr<DirectChatStateStore> stateStore)
        : DirectChatActionBase("AckDirectMessagesAction", std::move(stateStore)) {
    }

    core::contracts::OperationStatus AckDirectMessagesAction::execute(const core::contracts::IMessage& message) {
        if (!stateStore_) {
            return core::contracts::OperationStatus::failure("Direct chat state store is not configured.");
        }

        const core::contracts::TypedMessage<DirectChatCommand>* typedMessage = nullptr;
        const auto readStatus = readCommand(message, typedMessage);
        if (!readStatus.ok) {
            return readStatus;
        }
        return stateStore_->ackMessages(typedMessage->payload());
    }

    SearchUsersAction::SearchUsersAction(std::shared_ptr<DirectChatStateStore> stateStore)
        : DirectChatActionBase("SearchUsersAction", std::move(stateStore)) {
    }

    core::contracts::OperationStatus SearchUsersAction::execute(const core::contracts::IMessage& message) {
        if (!stateStore_) {
            return core::contracts::OperationStatus::failure("Direct chat state store is not configured.");
        }

        const core::contracts::TypedMessage<DirectChatCommand>* typedMessage = nullptr;
        const auto readStatus = readCommand(message, typedMessage);
        if (!readStatus.ok) {
            return readStatus;
        }
        return stateStore_->searchUsers(typedMessage->payload());
    }

} // namespace eds::server_new::features::direct_chat