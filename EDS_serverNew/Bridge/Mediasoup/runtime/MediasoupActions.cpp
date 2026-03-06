#include "Bridge/Mediasoup/runtime/MediasoupActions.h"

#include <utility>

namespace eds::server_new::mediasoup {

MediasoupActionBase::MediasoupActionBase(std::string name, std::shared_ptr<MediasoupStateStore> stateStore)
    : BaseModule(std::move(name), static_cast<ModuleId>(-1)),
      stateStore_(std::move(stateStore)) {
}

core::contracts::OperationStatus MediasoupActionBase::readCommand(
    const core::contracts::IMessage& message,
    const core::contracts::TypedMessage<MediasoupCommand>*& typedMessage) const {
    if (!stateStore_) {
        return core::contracts::OperationStatus::failure("Mediasoup state store is not configured.");
    }

    typedMessage = dynamic_cast<const core::contracts::TypedMessage<MediasoupCommand>*>(&message);
    if (!typedMessage) {
        return core::contracts::OperationStatus::failure("Mediasoup action expects TypedMessage<MediasoupCommand> payload.");
    }

    return core::contracts::OperationStatus::success();
}

bool MediasoupActionBase::onInitialize() {
    return true;
}

void MediasoupActionBase::onShutdown() {
}

CreateRoomAction::CreateRoomAction(std::shared_ptr<MediasoupStateStore> stateStore)
    : MediasoupActionBase("CreateRoomAction", std::move(stateStore)) {
}

core::contracts::OperationStatus CreateRoomAction::execute(const core::contracts::IMessage& message) {
    const core::contracts::TypedMessage<MediasoupCommand>* typedMessage = nullptr;
    const auto readStatus = readCommand(message, typedMessage);
    if (!readStatus.ok) {
        return readStatus;
    }
    return stateStore_->createRoom(typedMessage->payload());
}

JoinRoomAction::JoinRoomAction(std::shared_ptr<MediasoupStateStore> stateStore)
    : MediasoupActionBase("JoinRoomAction", std::move(stateStore)) {
}

core::contracts::OperationStatus JoinRoomAction::execute(const core::contracts::IMessage& message) {
    const core::contracts::TypedMessage<MediasoupCommand>* typedMessage = nullptr;
    const auto readStatus = readCommand(message, typedMessage);
    if (!readStatus.ok) {
        return readStatus;
    }
    return stateStore_->joinRoom(typedMessage->payload());
}

OpenTransportAction::OpenTransportAction(std::shared_ptr<MediasoupStateStore> stateStore)
    : MediasoupActionBase("OpenTransportAction", std::move(stateStore)) {
}

core::contracts::OperationStatus OpenTransportAction::execute(const core::contracts::IMessage& message) {
    const core::contracts::TypedMessage<MediasoupCommand>* typedMessage = nullptr;
    const auto readStatus = readCommand(message, typedMessage);
    if (!readStatus.ok) {
        return readStatus;
    }
    return stateStore_->openTransport(typedMessage->payload());
}

ProduceAction::ProduceAction(std::shared_ptr<MediasoupStateStore> stateStore)
    : MediasoupActionBase("ProduceAction", std::move(stateStore)) {
}

core::contracts::OperationStatus ProduceAction::execute(const core::contracts::IMessage& message) {
    const core::contracts::TypedMessage<MediasoupCommand>* typedMessage = nullptr;
    const auto readStatus = readCommand(message, typedMessage);
    if (!readStatus.ok) {
        return readStatus;
    }
    return stateStore_->startProducing(typedMessage->payload());
}

ConsumeAction::ConsumeAction(std::shared_ptr<MediasoupStateStore> stateStore)
    : MediasoupActionBase("ConsumeAction", std::move(stateStore)) {
}

core::contracts::OperationStatus ConsumeAction::execute(const core::contracts::IMessage& message) {
    const core::contracts::TypedMessage<MediasoupCommand>* typedMessage = nullptr;
    const auto readStatus = readCommand(message, typedMessage);
    if (!readStatus.ok) {
        return readStatus;
    }
    return stateStore_->consume(typedMessage->payload());
}

} // namespace eds::server_new::mediasoup
