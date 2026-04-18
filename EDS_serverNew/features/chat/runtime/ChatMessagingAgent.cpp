#include "features/chat/runtime/ChatMessagingAgent.h"

#include "features/chat/runtime/ChatActions.h"
#include "features/chat/runtime/ChatCommand.h"
#include "features/runtime/AgentActionRegistration.h"

#include <stdexcept>
#include <utility>

namespace eds::server_new::features::chat {

ChatMessagingAgent::ChatMessagingAgent(std::shared_ptr<ChatStateStore> stateStore)
    : BaseAgent("ChatMessagingAgent", static_cast<ModuleId>(-1)),
      stateStore_(std::move(stateStore)) {
    if (!stateStore_) {
        throw std::invalid_argument("ChatMessagingAgent requires a state store.");
    }
    eds::server_new::features::runtime::registerActionOrThrow(*this, std::string(kActionSendMessage), [stateStore = stateStore_]() {
        return std::make_unique<SendMessageAction>(stateStore);
    });
    eds::server_new::features::runtime::registerActionOrThrow(*this, std::string(kActionSyncMessages), [stateStore = stateStore_]() {
        return std::make_unique<SyncMessagesAction>(stateStore);
    });
    eds::server_new::features::runtime::registerActionOrThrow(*this, std::string(kActionAckMessages), [stateStore = stateStore_]() {
        return std::make_unique<AckMessagesAction>(stateStore);
    });
}

} // namespace eds::server_new::features::chat
