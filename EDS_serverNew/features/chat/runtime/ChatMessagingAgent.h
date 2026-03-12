#pragma once

#include "features/chat/runtime/ChatStateStore.h"
#include "modules/BaseAgent.h"

#include <memory>

namespace eds::server_new::features::chat {

class ChatMessagingAgent final : public BaseAgent {
public:
    explicit ChatMessagingAgent(std::shared_ptr<ChatStateStore> stateStore);

private:
    std::shared_ptr<ChatStateStore> stateStore_;
};

} // namespace eds::server_new::features::chat
