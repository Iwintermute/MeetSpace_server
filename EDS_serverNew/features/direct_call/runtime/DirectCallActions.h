#pragma once

#include "contracts/IMessage.h"
#include "contracts/TypedMessage.h"
#include "features/direct_call/runtime/DirectCallCommand.h"
#include "features/direct_call/runtime/DirectCallStateStore.h"
#include "interfaces/iAction.h"
#include "modules/BaseModule.h"

#include <memory>
#include <string>

namespace eds::server_new::features::direct_call {

    class DirectCallActionBase : public BaseModule, public iAction {
    public:
        DirectCallActionBase(std::string name, std::shared_ptr<DirectCallStateStore> stateStore);
        ~DirectCallActionBase() override = default;

    protected:
        core::contracts::OperationStatus readCommand(
            const core::contracts::IMessage& message,
            const core::contracts::TypedMessage<DirectCallCommand>*& typedMessage) const;

        bool onInitialize() override;
        void onShutdown() override;

        std::shared_ptr<DirectCallStateStore> stateStore_;
    };

    class CreateDirectCallAction final : public DirectCallActionBase {
    public:
        explicit CreateDirectCallAction(std::shared_ptr<DirectCallStateStore> stateStore);
        core::contracts::OperationStatus execute(const core::contracts::IMessage& message) override;
    };

    class AcceptDirectCallAction final : public DirectCallActionBase {
    public:
        explicit AcceptDirectCallAction(std::shared_ptr<DirectCallStateStore> stateStore);
        core::contracts::OperationStatus execute(const core::contracts::IMessage& message) override;
    };

    class DeclineDirectCallAction final : public DirectCallActionBase {
    public:
        explicit DeclineDirectCallAction(std::shared_ptr<DirectCallStateStore> stateStore);
        core::contracts::OperationStatus execute(const core::contracts::IMessage& message) override;
    };

    class HangupDirectCallAction final : public DirectCallActionBase {
    public:
        explicit HangupDirectCallAction(std::shared_ptr<DirectCallStateStore> stateStore);
        core::contracts::OperationStatus execute(const core::contracts::IMessage& message) override;
    };

} // namespace eds::server_new::features::direct_call