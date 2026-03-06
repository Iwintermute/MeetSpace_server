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

MediasoupSignalingAgent::MediasoupSignalingAgent(std::shared_ptr<MediasoupStateStore> stateStore)
    : BaseAgent("MediasoupSignalingAgent", static_cast<ModuleId>(-1)),
      stateStore_(std::move(stateStore)) {
    if (!stateStore_) {
        throw std::invalid_argument("MediasoupSignalingAgent requires a state store.");
    }

    registerActionOrThrow(*this, std::string(kActionCreateRoom), [stateStore = stateStore_]() {
        return std::make_unique<CreateRoomAction>(stateStore);
    });
    registerActionOrThrow(*this, std::string(kActionJoinRoom), [stateStore = stateStore_]() {
        return std::make_unique<JoinRoomAction>(stateStore);
    });
    registerActionOrThrow(*this, std::string(kActionOpenTransport), [stateStore = stateStore_]() {
        return std::make_unique<OpenTransportAction>(stateStore);
    });
    registerActionOrThrow(*this, std::string(kActionProduce), [stateStore = stateStore_]() {
        return std::make_unique<ProduceAction>(stateStore);
    });
    registerActionOrThrow(*this, std::string(kActionConsume), [stateStore = stateStore_]() {
        return std::make_unique<ConsumeAction>(stateStore);
    });
}

} // namespace eds::server_new::mediasoup
