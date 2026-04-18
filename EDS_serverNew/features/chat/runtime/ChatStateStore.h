#pragma once

#include "Auth/SessionAuthStore.h"
#include "contracts/Primitives.h"
#include "features/chat/runtime/ChatCommand.h"
#include "infrastructure/db/MessengerRepository.h"

#include <memory>

namespace eds::server_new::features::chat {

    class ChatStateStore final {
    public:
        ChatStateStore(
            std::shared_ptr<eds::server_new::auth::SessionAuthStore> sessionStore,
            std::shared_ptr<eds::server_new::infrastructure::db::MessengerRepository> repository);

        core::contracts::OperationStatus sendMessage(const ChatCommand& command);
        core::contracts::OperationStatus syncMessages(const ChatCommand& command);
        core::contracts::OperationStatus ackMessages(const ChatCommand& command);

    private:
        std::shared_ptr<eds::server_new::auth::SessionAuthStore> sessionStore_;
        std::shared_ptr<eds::server_new::infrastructure::db::MessengerRepository> repository_;
    };

} // namespace eds::server_new::features::chat