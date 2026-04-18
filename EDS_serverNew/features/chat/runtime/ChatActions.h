#pragma once

#include "contracts/IMessage.h"
#include "contracts/TypedMessage.h"
#include "features/chat/runtime/ChatCommand.h"
#include "features/chat/runtime/ChatStateStore.h"
#include "interfaces/iAction.h"
#include "modules/BaseModule.h"

#include <memory>
#include <string>

namespace eds::server_new::features::chat {

class ChatActionBase : public BaseModule, public iAction {
public:
    ChatActionBase(std::string name, std::shared_ptr<ChatStateStore> stateStore);
    ~ChatActionBase() override = default;

protected:
    core::contracts::OperationStatus readCommand(
        const core::contracts::IMessage& message,
        const core::contracts::TypedMessage<ChatCommand>*& typedMessage) const;

    bool onInitialize() override;
    void onShutdown() override;

    std::shared_ptr<ChatStateStore> stateStore_;
};

class SendMessageAction final : public ChatActionBase {
public:
    explicit SendMessageAction(std::shared_ptr<ChatStateStore> stateStore);
    core::contracts::OperationStatus execute(const core::contracts::IMessage& message) override;
};

class SyncMessagesAction final : public ChatActionBase {
public:
    explicit SyncMessagesAction(std::shared_ptr<ChatStateStore> stateStore);
    core::contracts::OperationStatus execute(const core::contracts::IMessage& message) override;
};

class AckMessagesAction final : public ChatActionBase {
public:
    explicit AckMessagesAction(std::shared_ptr<ChatStateStore> stateStore);
    core::contracts::OperationStatus execute(const core::contracts::IMessage& message) override;
};

} // namespace eds::server_new::features::chat
