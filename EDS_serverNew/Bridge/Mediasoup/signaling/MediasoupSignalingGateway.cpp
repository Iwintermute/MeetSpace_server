#include "Bridge/Mediasoup/signaling/MediasoupSignalingGateway.h"

#include "App/ApplicationCore.h"
#include "utils/WordGenerator.h"

#include <chrono>
#include <iostream>
#include <nlohmann/json.hpp>
#include <thread>

namespace eds::server_new::mediasoup::signaling {
using json = nlohmann::json;

MediasoupSignalingGateway::MediasoupSignalingGateway(ApplicationApi& app, unsigned short wsPort)
    : app_(app),
      wsPort_(wsPort) {
}

MediasoupSignalingGateway::~MediasoupSignalingGateway() {
    stop();
}

bool MediasoupSignalingGateway::start() {
    if (running_.load()) {
        return true;
    }

    if (!ioContext_.init()) {
        return false;
    }

    wsServer_ = std::make_unique<transport::WebSocketServer>(ioContext_.io(), wsPort_);
    wsServer_->setOnConnected([this](void* session) { onConnected(session); });
    wsServer_->setOnDisconnected([this](void* session) { onDisconnected(session); });
    wsServer_->setOnMessage([this](const std::string& text, void* session) { onMessage(text, session); });

    if (!wsServer_->start()) {
        wsServer_.reset();
        ioContext_.stop();
        return false;
    }

    if (!ioContext_.start()) {
        wsServer_->stop();
        wsServer_.reset();
        ioContext_.stop();
        return false;
    }

    running_.store(true);
    return true;
}

void MediasoupSignalingGateway::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    if (wsServer_) {
        wsServer_->stop();
        wsServer_.reset();
    }
    ioContext_.stop();

    std::lock_guard<std::mutex> lock(peersMutex_);
    sessionToPeer_.clear();
    peerToSession_.clear();
}

void MediasoupSignalingGateway::wait() {
    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
}

void MediasoupSignalingGateway::onConnected(void* session) {
    const auto trustedPeer = resolveTrustedPeer(session);
    if (!wsServer_) {
        return;
    }

    json assign{
        { "type", "peer_assigned" },
        { "peer", trustedPeer }
    };
    wsServer_->sendText(session, assign.dump());
}

void MediasoupSignalingGateway::onDisconnected(void* session) {
    std::lock_guard<std::mutex> lock(peersMutex_);
    auto iterator = sessionToPeer_.find(session);
    if (iterator == sessionToPeer_.end()) {
        return;
    }
    app_.notifyFeatureSessionDisconnected(iterator->second, reinterpret_cast<std::uintptr_t>(session));

    peerToSession_.erase(iterator->second);
    sessionToPeer_.erase(iterator);
}

void MediasoupSignalingGateway::onMessage(const std::string& text, void* session) {
    if (!wsServer_) {
        return;
    }

    json response;
    try {
        const auto trustedPeer = resolveTrustedPeer(session);
        const auto request = json::parse(text);
        const auto objectType = request.value("object", std::string(kRouteObject));
        const auto agentType = request.value("agent", std::string{});
        const auto actionType = request.value("action", std::string{});

        if (actionType.empty()) {
            response = {
                { "type", "dispatch_result" },
                { "ok", false },
                { "message", "action must not be empty." }
            };
            wsServer_->sendText(session, response.dump());
            return;
        }

        const auto context = request.value("ctx", json::object());
        eds::server_new::features::runtime::FeatureDispatchRequest dispatchRequest;
        dispatchRequest.sessionHandle = reinterpret_cast<std::uintptr_t>(session);
        dispatchRequest.peerId = trustedPeer;
        dispatchRequest.objectType = objectType;
        dispatchRequest.agentType = agentType;
        dispatchRequest.actionType = actionType;
        dispatchRequest.context = context;

        auto dispatchResult = app_.dispatchFeature(dispatchRequest);
        const auto& status = dispatchResult.status;
        const auto effectiveAgent = dispatchResult.effectiveAgent.empty()
            ? agentType
            : dispatchResult.effectiveAgent;
        response = {
            { "type", "dispatch_result" },
            { "object", objectType },
            { "agent", effectiveAgent },
            { "action", actionType },
            { "peer", trustedPeer },
            { "ok", status.ok },
            { "message", status.message }
        };

        for (const auto& event : dispatchResult.outboundEvents) {
            wsServer_->sendText(session, event.dump());
        }
    } catch (const std::exception& ex) {
        response = {
            { "type", "dispatch_result" },
            { "ok", false },
            { "message", std::string("invalid request: ") + ex.what() }
        };
    }

    wsServer_->sendText(session, response.dump());
}

std::string MediasoupSignalingGateway::resolveTrustedPeer(void* session) {
    std::lock_guard<std::mutex> lock(peersMutex_);
    auto iterator = sessionToPeer_.find(session);
    if (iterator != sessionToPeer_.end()) {
        return iterator->second;
    }
    auto peer = utils::WordGenerator(12);
    while (peerToSession_.find(peer) != peerToSession_.end()) {
        peer = utils::WordGenerator(12);
    }

    sessionToPeer_.emplace(session, peer);
    peerToSession_.emplace(peer, session);
    return peer;
}

} // namespace eds::server_new::mediasoup::signaling
