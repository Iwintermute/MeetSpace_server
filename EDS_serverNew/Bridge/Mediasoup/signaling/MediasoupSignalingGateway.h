#pragma once

#include "Bridge/Mediasoup/runtime/MediasoupCommand.h"
#include "Bridge/Mediasoup/transport/NetIoContext.h"
#include "Bridge/Mediasoup/transport/WebSocketServer.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

class ApplicationApi;

namespace eds::server_new::mediasoup::signaling {

class MediasoupSignalingGateway {
public:
    MediasoupSignalingGateway(ApplicationApi& app, unsigned short wsPort);
    ~MediasoupSignalingGateway();

    bool start();
    void stop();
    void wait();

private:
    void onConnected(void* session);
    void onDisconnected(void* session);
    void onMessage(const std::string& text, void* session);
    std::string resolveTrustedPeer(void* session);

private:
    ApplicationApi& app_;
    unsigned short wsPort_ = 0;
    transport::NetIoContext ioContext_;
    std::unique_ptr<transport::WebSocketServer> wsServer_;
    std::atomic<bool> running_ = false;

    std::mutex peersMutex_;
    std::unordered_map<void*, std::string> sessionToPeer_;
    std::unordered_map<std::string, void*> peerToSession_;
};

} // namespace eds::server_new::mediasoup::signaling
