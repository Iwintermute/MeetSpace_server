#include "features/chat/runtime/ChatActions.h"

#include <utility>

namespace eds::server_new::features::chat {

ChatActionBase::ChatActionBase(std::string name, std::shared_ptr<ChatStateStore> stateStore)
    : BaseModule(std::move(name), static_cast<ModuleId>(-1)),
      stateStore_(std::move(stateStore)) {
}

core::contracts::OperationStatus ChatActionBase::readCommand(
    const core::contracts::IMessage& message,
    const core::contracts::TypedMessage<ChatCommand>*& typedMessage) const {
    typedMessage = dynamic_cast<const core::contracts::TypedMessage<ChatCommand>*>(&message);
    if (!typedMessage) {
        return core::contracts::OperationStatus::failure("Chat action expects TypedMessage<ChatCommand> payload.");
    }
    return core::contracts::OperationStatus::success();
}

bool ChatActionBase::onInitialize() {
    return true;
}

void ChatActionBase::onShutdown() {
}

SendMessageAction::SendMessageAction(std::shared_ptr<ChatStateStore> stateStore)
    : ChatActionBase("SendMessageAction", std::move(stateStore)) {
}

core::contracts::OperationStatus SendMessageAction::execute(const core::contracts::IMessage& message) {
    if (!stateStore_) {
        return core::contracts::OperationStatus::failure("Chat state store is not configured.");
    }

    const core::contracts::TypedMessage<ChatCommand>* typedMessage = nullptr;
    const auto readStatus = readCommand(message, typedMessage);
    if (!readStatus.ok) {
        return readStatus;
    }
    return stateStore_->sendMessage(typedMessage->payload());
}

SyncMessagesAction::SyncMessagesAction(std::shared_ptr<ChatStateStore> stateStore)
    : ChatActionBase("SyncMessagesAction", std::move(stateStore)) {
}

core::contracts::OperationStatus SyncMessagesAction::execute(const core::contracts::IMessage& message) {
    if (!stateStore_) {
        return core::contracts::OperationStatus::failure("Chat state store is not configured.");
    }

    const core::contracts::TypedMessage<ChatCommand>* typedMessage = nullptr;
    const auto readStatus = readCommand(message, typedMessage);
    if (!readStatus.ok) {
        return readStatus;
    }
    return stateStore_->syncMessages(typedMessage->payload());
}

AckMessagesAction::AckMessagesAction(std::shared_ptr<ChatStateStore> stateStore)
    : ChatActionBase("AckMessagesAction", std::move(stateStore)) {
}

core::contracts::OperationStatus AckMessagesAction::execute(const core::contracts::IMessage& message) {
    if (!stateStore_) {
        return core::contracts::OperationStatus::failure("Chat state store is not configured.");
    }

    const core::contracts::TypedMessage<ChatCommand>* typedMessage = nullptr;
    const auto readStatus = readCommand(message, typedMessage);
    if (!readStatus.ok) {
        return readStatus;
    }
    return stateStore_->ackMessages(typedMessage->payload());
}

} // namespace eds::server_new::features::chat
