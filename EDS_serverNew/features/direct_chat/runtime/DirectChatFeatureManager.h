#pragma once

#include "features/direct_chat/runtime/DirectChatStateStore.h"
#include "modules/BaseFeatureManager.h"

#include <memory>

namespace eds::server_new::features::direct_chat {

    class DirectChatFeatureManager final : public BaseFeatureManager {
    public:
        explicit DirectChatFeatureManager(std::shared_ptr<DirectChatStateStore> stateStore);

    private:
        std::shared_ptr<DirectChatStateStore> stateStore_;
    };

} // namespace eds::server_new::features::direct_chat