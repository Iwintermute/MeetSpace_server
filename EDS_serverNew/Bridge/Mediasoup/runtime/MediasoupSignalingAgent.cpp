#include "Bridge/Mediasoup/runtime/MediasoupSignalingAgent.h"

#include "Bridge/Mediasoup/runtime/MediasoupActions.h"
#include "Bridge/Mediasoup/runtime/MediasoupCommand.h"

#include <stdexcept>
#include <utility>

namespace {
void registerActionOrThrow(BaseAgent& agent, std::string actionKey, iAgent::tActionFactory factory) {
    auto status = agent.registerAction(std::move(actionKey), std::move(factory));
    if (!status.ok) {
        throw std::runtime_error(status.message);
    }
}
}

namespace eds::server_new::mediasoup {
MediasoupSignalingAgent::MediasoupSignalingAgent(
    std::shared_ptr<MediasoupStateStore> stateStore,
    std::shared_ptr<MediasoupRtcBridge> rtcBridge)
    : BaseAgent("MediasoupSignalingAgent", static_cast<ModuleId>(-1)),
      stateStore_(std::move(stateStore)),
      rtcBridge_(std::move(rtcBridge)) {
    if (!stateStore_ || !rtcBridge_) {
        throw std::invalid_argument("MediasoupSignalingAgent requires a state store.");
    }
    registerActionOrThrow(*this, std::string(kActionCreateRoom), [stateStore = stateStore_, rtcBridge = rtcBridge_]() {
        return std::make_unique<CreateRoomAction>(stateStore, rtcBridge);
    });
    registerActionOrThrow(*this, std::string(kActionJoinRoom), [stateStore = stateStore_, rtcBridge = rtcBridge_]() {
        return std::make_unique<JoinRoomAction>(stateStore, rtcBridge);
    });
    registerActionOrThrow(*this, std::string(kActionLeaveRoom), [stateStore = stateStore_, rtcBridge = rtcBridge_]() {
        return std::make_unique<LeaveRoomAction>(stateStore, rtcBridge);
    });
    registerActionOrThrow(*this, std::string(kActionConnectSession), [stateStore = stateStore_, rtcBridge = rtcBridge_]() {
        return std::make_unique<JoinRoomAction>(stateStore, rtcBridge);
    });
    registerActionOrThrow(*this, std::string(kActionDisconnectSession), [stateStore = stateStore_, rtcBridge = rtcBridge_]() {
        return std::make_unique<LeaveRoomAction>(stateStore, rtcBridge);
    });
    registerActionOrThrow(*this, std::string(kActionOpenTransport), [stateStore = stateStore_, rtcBridge = rtcBridge_]() {
        return std::make_unique<OpenTransportAction>(stateStore, rtcBridge);
    });
    registerActionOrThrow(*this, std::string(kActionProduce), [stateStore = stateStore_, rtcBridge = rtcBridge_]() {
        return std::make_unique<ProduceAction>(stateStore, rtcBridge);
    });
    registerActionOrThrow(*this, std::string(kActionConsume), [stateStore = stateStore_, rtcBridge = rtcBridge_]() {
        return std::make_unique<ConsumeAction>(stateStore, rtcBridge);
    });
    registerActionOrThrow(*this, std::string(kActionWebrtcOffer), [stateStore = stateStore_, rtcBridge = rtcBridge_]() {
        return std::make_unique<WebRtcOfferAction>(stateStore, rtcBridge);
    });
    registerActionOrThrow(*this, std::string(kActionWebrtcIce), [stateStore = stateStore_, rtcBridge = rtcBridge_]() {
        return std::make_unique<WebRtcIceAction>(stateStore, rtcBridge);
    });
    registerActionOrThrow(*this, std::string(kActionWebrtcClose), [stateStore = stateStore_, rtcBridge = rtcBridge_]() {
        return std::make_unique<WebRtcCloseAction>(stateStore, rtcBridge);
    });
}

} // namespace eds::server_new::mediasoup
