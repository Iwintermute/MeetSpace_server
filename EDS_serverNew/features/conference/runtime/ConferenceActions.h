#pragma once

#include "contracts/IMessage.h"
#include "contracts/TypedMessage.h"
#include "features/conference/runtime/ConferenceCommand.h"
#include "features/conference/runtime/ConferenceStateStore.h"
#include "interfaces/iAction.h"
#include "modules/BaseModule.h"

#include <memory>
#include <string>

namespace eds::server_new::features::conference {

class ConferenceActionBase : public BaseModule, public iAction {
public:
    ConferenceActionBase(std::string name, std::shared_ptr<ConferenceStateStore> stateStore);
    ~ConferenceActionBase() override = default;

protected:
    core::contracts::OperationStatus readCommand(
        const core::contracts::IMessage& message,
        const core::contracts::TypedMessage<ConferenceCommand>*& typedMessage) const;

    bool onInitialize() override;
    void onShutdown() override;

    std::shared_ptr<ConferenceStateStore> stateStore_;
};

class CreateConferenceAction final : public ConferenceActionBase {
public:
    explicit CreateConferenceAction(std::shared_ptr<ConferenceStateStore> stateStore);
    core::contracts::OperationStatus execute(const core::contracts::IMessage& message) override;
};

class GetConferenceAction final : public ConferenceActionBase {
public:
    explicit GetConferenceAction(std::shared_ptr<ConferenceStateStore> stateStore);
    core::contracts::OperationStatus execute(const core::contracts::IMessage& message) override;
};

class CloseConferenceAction final : public ConferenceActionBase {
public:
    explicit CloseConferenceAction(std::shared_ptr<ConferenceStateStore> stateStore);
    core::contracts::OperationStatus execute(const core::contracts::IMessage& message) override;
};

class JoinConferenceAction final : public ConferenceActionBase {
public:
    explicit JoinConferenceAction(std::shared_ptr<ConferenceStateStore> stateStore);
    core::contracts::OperationStatus execute(const core::contracts::IMessage& message) override;
};

class LeaveConferenceAction final : public ConferenceActionBase {
public:
    explicit LeaveConferenceAction(std::shared_ptr<ConferenceStateStore> stateStore);
    core::contracts::OperationStatus execute(const core::contracts::IMessage& message) override;
};

class ListMembersAction final : public ConferenceActionBase {
public:
    explicit ListMembersAction(std::shared_ptr<ConferenceStateStore> stateStore);
    core::contracts::OperationStatus execute(const core::contracts::IMessage& message) override;
};

} // namespace eds::server_new::features::conference
