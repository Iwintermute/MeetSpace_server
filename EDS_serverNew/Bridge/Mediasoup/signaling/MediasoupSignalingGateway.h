#pragma once

#include "Bridge/Mediasoup/runtime/MediasoupCommand.h"
#include "Bridge/Mediasoup/transport/NetIoContext.h"
#include "Bridge/Mediasoup/transport/WebSocketServer.h"
#include "features/events/AudioSessionEvents.h"
#include "features/runtime/FeatureEventBus.h"

#include <boost/signals2/connection.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

class ApplicationApi;

namespace eds::server_new::mediasoup::signaling {

class MediasoupSignalingGateway {
public:
    MediasoupSignalingGateway(
        ApplicationApi& app,
        unsigned short wsPort,
        bool allowDirectMediasoupDebug = false,
        bool debugMode = false);
    ~MediasoupSignalingGateway();

    bool start();
    void stop();
    void wait();

private:
    void onConnected(void* session);
    void onDisconnected(void* session);
    void onMessage(const std::string& text, void* session);
    void onAudioSessionLifecycleEvent(const eds::server_new::features::events::AudioSessionLifecycleEvent& event);
    std::string resolveTrustedPeer(void* session);
    bool sendTextToPeer(std::string_view peerId, const std::string& text);

private:
    ApplicationApi& app_;
    unsigned short wsPort_ = 0;
    bool allowDirectMediasoupDebug_ = false;
    bool debugMode_ = false;
    transport::NetIoContext ioContext_;
    std::unique_ptr<transport::WebSocketServer> wsServer_;
    std::atomic<bool> running_ = false;
    std::atomic<std::uint64_t> receivedMessages_ = 0;
    std::atomic<std::uint64_t> forwardedEvents_ = 0;
    std::shared_ptr<eds::server_new::features::runtime::FeatureEventBus> featureEventBus_;
    boost::signals2::scoped_connection audioSessionLifecycleSubscription_;

    std::mutex peersMutex_;
    std::unordered_map<void*, std::string> sessionToPeer_;
    std::unordered_map<std::string, void*> peerToSession_;
};

} // namespace eds::server_new::mediasoup::signaling
