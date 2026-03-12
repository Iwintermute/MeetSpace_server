#pragma once

#include "features/chat/runtime/ChatStateStore.h"
#include "modules/BaseFeatureManager.h"

#include <memory>

namespace eds::server_new::features::chat {

class ChatFeatureManager final : public BaseFeatureManager {
public:
    explicit ChatFeatureManager(std::shared_ptr<ChatStateStore> stateStore);

private:
    std::shared_ptr<ChatStateStore> stateStore_;
};

} // namespace eds::server_new::features::chat
