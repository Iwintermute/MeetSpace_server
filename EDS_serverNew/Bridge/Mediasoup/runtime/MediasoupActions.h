#pragma once

#include "Bridge/Mediasoup/runtime/MediasoupRtcBridge.h"
#include "Bridge/Mediasoup/runtime/MediasoupStateStore.h"
#include "contracts/IMessage.h"
#include "contracts/TypedMessage.h"
#include "interfaces/iAction.h"
#include "modules/BaseModule.h"

#include <memory>
#include <string>

namespace eds::server_new::mediasoup {

class MediasoupActionBase : public BaseModule, public iAction {
public:
    MediasoupActionBase(
        std::string name,
        std::shared_ptr<MediasoupStateStore> stateStore,
        std::shared_ptr<MediasoupRtcBridge> rtcBridge);
    ~MediasoupActionBase() override = default;

protected:
    core::contracts::OperationStatus readCommand(
        const core::contracts::IMessage& message,
        const core::contracts::TypedMessage<MediasoupCommand>*& typedMessage) const;

    bool onInitialize() override;
    void onShutdown() override;

    std::shared_ptr<MediasoupStateStore> stateStore_;
    std::shared_ptr<MediasoupRtcBridge> rtcBridge_;
};

class CreateRoomAction final : public MediasoupActionBase {
public:
    CreateRoomAction(
        std::shared_ptr<MediasoupStateStore> stateStore,
        std::shared_ptr<MediasoupRtcBridge> rtcBridge);
    core::contracts::OperationStatus execute(const core::contracts::IMessage& message) override;
};

class JoinRoomAction final : public MediasoupActionBase {
public:
    JoinRoomAction(
        std::shared_ptr<MediasoupStateStore> stateStore,
        std::shared_ptr<MediasoupRtcBridge> rtcBridge);
    core::contracts::OperationStatus execute(const core::contracts::IMessage& message) override;
};

class OpenTransportAction final : public MediasoupActionBase {
public:
    OpenTransportAction(
        std::shared_ptr<MediasoupStateStore> stateStore,
        std::shared_ptr<MediasoupRtcBridge> rtcBridge);
    core::contracts::OperationStatus execute(const core::contracts::IMessage& message) override;
};

class ProduceAction final : public MediasoupActionBase {
public:
    ProduceAction(
        std::shared_ptr<MediasoupStateStore> stateStore,
        std::shared_ptr<MediasoupRtcBridge> rtcBridge);
    core::contracts::OperationStatus execute(const core::contracts::IMessage& message) override;
};

class ConsumeAction final : public MediasoupActionBase {
public:
    ConsumeAction(
        std::shared_ptr<MediasoupStateStore> stateStore,
        std::shared_ptr<MediasoupRtcBridge> rtcBridge);
    core::contracts::OperationStatus execute(const core::contracts::IMessage& message) override;
};

class WebRtcOfferAction final : public MediasoupActionBase {
public:
    WebRtcOfferAction(
        std::shared_ptr<MediasoupStateStore> stateStore,
        std::shared_ptr<MediasoupRtcBridge> rtcBridge);
    core::contracts::OperationStatus execute(const core::contracts::IMessage& message) override;
};

class WebRtcIceAction final : public MediasoupActionBase {
public:
    WebRtcIceAction(
        std::shared_ptr<MediasoupStateStore> stateStore,
        std::shared_ptr<MediasoupRtcBridge> rtcBridge);
    core::contracts::OperationStatus execute(const core::contracts::IMessage& message) override;
};

class WebRtcCloseAction final : public MediasoupActionBase {
public:
    WebRtcCloseAction(
        std::shared_ptr<MediasoupStateStore> stateStore,
        std::shared_ptr<MediasoupRtcBridge> rtcBridge);
    core::contracts::OperationStatus execute(const core::contracts::IMessage& message) override;
};

} // namespace eds::server_new::mediasoup
