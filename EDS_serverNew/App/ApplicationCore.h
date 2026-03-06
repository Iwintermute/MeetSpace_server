#pragma once
#include "Bridge/Mediasoup/runtime/MediasoupCommand.h"
#include "Bridge/Mediasoup/runtime/MediasoupFeatureManager.h"
#include "Bridge/Mediasoup/runtime/MediasoupRtcBridge.h"

#include "contracts/IModuleRegistry.h"
#include "contracts/Primitives.h"
#include "contracts/TypedMessage.h"
#include "runtime/MessageDispatcher.h"
#include <memory>
#include <vector>

class ApplicationApi{
public:
    explicit ApplicationApi(std::shared_ptr<core::contracts::IModuleRegistry> registry = nullptr);
public:
    bool init();

    bool start();
    core::contracts::OperationStatus registerMediasoup();
    core::contracts::OperationStatus dispatchMediasoup(
        const core::contracts::MessageRoute& route,
        const eds::server_new::mediasoup::MediasoupCommand& command);
    std::vector<eds::server_new::mediasoup::MediasoupSignalingEvent> pollMediasoupEventsForPeer(const std::string& peerId);
    void notifyMediasoupSessionDisconnected(const std::string& peerId, std::uintptr_t sessionHandle);

private:
    std::shared_ptr<core::contracts::IModuleRegistry> CoreRegistry;//Реестр модулей
    core::runtime::MessageDispatcher dispatcher_;
    std::shared_ptr<eds::server_new::mediasoup::MediasoupFeatureManager> mediasoupManager_;
    std::shared_ptr<eds::server_new::mediasoup::MediasoupStateStore> mediasoupState_;
    std::shared_ptr<eds::server_new::mediasoup::MediasoupRtcBridge> mediasoupRtcBridge_;
    bool mediasoupRegistered_ = false;
};
