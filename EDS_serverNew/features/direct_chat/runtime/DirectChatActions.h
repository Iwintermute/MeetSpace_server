#pragma once

#include "contracts/IMessage.h"
#include "contracts/TypedMessage.h"
#include "features/direct_chat/runtime/DirectChatCommand.h"
#include "features/direct_chat/runtime/DirectChatStateStore.h"
#include "interfaces/iAction.h"
#include "modules/BaseModule.h"

#include <memory>
#include <string>

namespace eds::server_new::features::direct_chat {

    class DirectChatActionBase : public BaseModule, public iAction {
    public:
        DirectChatActionBase(std::string name, std::shared_ptr<DirectChatStateStore> stateStore);
        ~DirectChatActionBase() override = default;

    protected:
        core::contracts::OperationStatus readCommand(
            const core::contracts::IMessage& message,
            const core::contracts::TypedMessage<DirectChatCommand>*& typedMessage) const;

        bool onInitialize() override;
        void onShutdown() override;

        std::shared_ptr<DirectChatStateStore> stateStore_;
    };

    class SendDirectMessageAction final : public DirectChatActionBase {
    public:
        explicit SendDirectMessageAction(std::shared_ptr<DirectChatStateStore> stateStore);
        core::contracts::OperationStatus execute(const core::contracts::IMessage& message) override;
    };

    class ListDirectThreadsAction final : public DirectChatActionBase {
    public:
        explicit ListDirectThreadsAction(std::shared_ptr<DirectChatStateStore> stateStore);
        core::contracts::OperationStatus execute(const core::contracts::IMessage& message) override;
    };

    class SyncDirectMessagesAction final : public DirectChatActionBase {
    public:
        explicit SyncDirectMessagesAction(std::shared_ptr<DirectChatStateStore> stateStore);
        core::contracts::OperationStatus execute(const core::contracts::IMessage& message) override;
    };

    class AckDirectMessagesAction final : public DirectChatActionBase {
    public:
        explicit AckDirectMessagesAction(std::shared_ptr<DirectChatStateStore> stateStore);
        core::contracts::OperationStatus execute(const core::contracts::IMessage& message) override;
    };

} // namespace eds::server_new::features::direct_chat