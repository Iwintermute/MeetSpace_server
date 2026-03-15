#pragma once

#include "features/chat/runtime/ChatFeatureManager.h"
#include "features/chat/runtime/ChatStateStore.h"
#include "features/runtime/FeatureEventBus.h"
#include "features/runtime/FeatureRegistry.h"
#include "modules/BaseModule.h"

#include <boost/signals2/connection.hpp>
#include <memory>
#include <string_view>

namespace eds::server_new::features::chat {

class ChatFeatureModule final : public BaseModule, public eds::server_new::features::runtime::IFeatureModule {
public:
    ChatFeatureModule();
    ~ChatFeatureModule() override = default;

    std::string_view objectType() const override;
    std::string_view defaultAgent() const override;
    core::contracts::OperationStatus ensureRegistered(core::runtime::MessageDispatcher& dispatcher) override;
    eds::server_new::features::runtime::FeatureDispatchResult dispatch(
        const eds::server_new::features::runtime::FeatureDispatchRequest& request,
        core::runtime::MessageDispatcher& dispatcher) override;

private:
    bool onInitialize() override;
    void onShutdown() override;

private:
    bool registered_ = false;
    std::shared_ptr<ChatFeatureManager> manager_;
    std::shared_ptr<ChatStateStore> stateStore_;
    std::shared_ptr<eds::server_new::features::runtime::FeatureEventBus> eventBus_;
    boost::signals2::scoped_connection conferenceSnapshotSubscription_;
};

} // namespace eds::server_new::features::chat
