#pragma once

#include "features/direct_call/runtime/DirectCallStateStore.h"
#include "modules/BaseFeatureManager.h"

#include <memory>

namespace eds::server_new::features::direct_call {

    class DirectCallFeatureManager final : public BaseFeatureManager {
    public:
        explicit DirectCallFeatureManager(std::shared_ptr<DirectCallStateStore> stateStore);

    private:
        std::shared_ptr<DirectCallStateStore> stateStore_;
    };

} // namespace eds::server_new::features::direct_call