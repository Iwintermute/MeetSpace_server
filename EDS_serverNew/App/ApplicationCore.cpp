#include "App/ApplicationCore.h"
#include "managers/ModuleRegistry.h"
#include <utility>

ApplicationApi::ApplicationApi(std::shared_ptr<core::contracts::IModuleRegistry> registry)
    : CoreRegistry(std::move(registry)) {
    if (!CoreRegistry) {
        CoreRegistry = ModuleRegistry::instance();
    }
}
core::contracts::OperationStatus ApplicationApi::dispatchMediasoup(
    const core::contracts::MessageRoute& route,
    const eds::server_new::mediasoup::MediasoupCommand& command) {
    const auto registrationStatus = registerMediasoup();
    if (!registrationStatus.ok) {
        return registrationStatus;
    }

    core::contracts::TypedMessage<eds::server_new::mediasoup::MediasoupCommand> payload(command);
    return dispatcher_.dispatch(route, payload);
}

bool ApplicationApi::init(){
    if (!CoreRegistry) {
        return false;
    }

    if (!CoreRegistry->initializeAll()) {
        return false;
    }

    const auto mediasoupStatus = registerMediasoup();
    return mediasoupStatus.ok;
}

bool ApplicationApi::start(){
    return CoreRegistry != nullptr && mediasoupRegistered_;
}

core::contracts::OperationStatus ApplicationApi::registerMediasoup() {
    if (mediasoupRegistered_) {
        return core::contracts::OperationStatus::success();
    }

    if (!mediasoupState_) {
        mediasoupState_ = std::make_shared<eds::server_new::mediasoup::MediasoupStateStore>();
    }
    if (!mediasoupManager_) {
        mediasoupManager_ = std::make_shared<eds::server_new::mediasoup::MediasoupFeatureManager>(mediasoupState_);
    }

    auto status = dispatcher_.registerManager(std::string(eds::server_new::mediasoup::kRouteObject), mediasoupManager_);
    if (!status.ok) {
        return status;
    }

    mediasoupRegistered_ = true;
    return core::contracts::OperationStatus::success();
}

