#include "features/chat/runtime/ChatMessagingAgent.h"

#include "features/chat/runtime/ChatActions.h"
#include "features/chat/runtime/ChatCommand.h"

#include <stdexcept>
#include <utility>

namespace {

void registerActionOrThrow(BaseAgent& agent, std::string actionKey, iAgent::tActionFactory factory) {
    auto status = agent.registerAction(std::move(actionKey), std::move(factory));
    if (!status.ok) {
        throw std::runtime_error(status.message);
    }
}

} // namespace

namespace eds::server_new::features::chat {

ChatMessagingAgent::ChatMessagingAgent(std::shared_ptr<ChatStateStore> stateStore)
    : BaseAgent("ChatMessagingAgent", static_cast<ModuleId>(-1)),
      stateStore_(std::move(stateStore)) {
    if (!stateStore_) {
        throw std::invalid_argument("ChatMessagingAgent requires a state store.");
    }

    registerActionOrThrow(*this, std::string(kActionSendMessage), [stateStore = stateStore_]() {
        return std::make_unique<SendMessageAction>(stateStore);
    });
}

} // namespace eds::server_new::features::chat
