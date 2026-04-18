#include "features/direct_chat/runtime/DirectChatMessagingAgent.h"

#include "features/direct_chat/runtime/DirectChatActions.h"
#include "features/direct_chat/runtime/DirectChatCommand.h"
#include "features/runtime/AgentActionRegistration.h"

#include <stdexcept>
#include <utility>

namespace eds::server_new::features::direct_chat {

    DirectChatMessagingAgent::DirectChatMessagingAgent(std::shared_ptr<DirectChatStateStore> stateStore)
        : BaseAgent("DirectChatMessagingAgent", static_cast<ModuleId>(-1)),
        stateStore_(std::move(stateStore)) {
        if (!stateStore_) {
            throw std::invalid_argument("DirectChatMessagingAgent requires a state store.");
        }

        eds::server_new::features::runtime::registerActionOrThrow(*this, std::string(kActionSendDirectMessage), [stateStore = stateStore_]() {
            return std::make_unique<SendDirectMessageAction>(stateStore);
            });
        eds::server_new::features::runtime::registerActionOrThrow(*this, std::string(kActionListDirectThreads), [stateStore = stateStore_]() {
            return std::make_unique<ListDirectThreadsAction>(stateStore);
            });
        eds::server_new::features::runtime::registerActionOrThrow(*this, std::string(kActionSyncDirectMessages), [stateStore = stateStore_]() {
            return std::make_unique<SyncDirectMessagesAction>(stateStore);
            });
        eds::server_new::features::runtime::registerActionOrThrow(*this, std::string(kActionAckDirectMessages), [stateStore = stateStore_]() {
            return std::make_unique<AckDirectMessagesAction>(stateStore);
            });
    }

} // namespace eds::server_new::features::direct_chat