#include "Bridge/Mediasoup/signaling/MediasoupSignalingGateway.h"

#include "App/ApplicationCore.h"
#include "auth/runtime/AuthServices.h"
#include "auth/runtime/AuthCommand.h"
#include "utils/WordGenerator.h"

#include <chrono>
#include <iostream>
#include <nlohmann/json.hpp>
#include <thread>
#include <unordered_set>

namespace eds::server_new::mediasoup::signaling {
    using json = nlohmann::json;

    MediasoupSignalingGateway::MediasoupSignalingGateway(
        ApplicationApi& app,
        unsigned short wsPort,
        bool allowDirectMediasoupDebug,
        bool debugMode)
        : app_(app),
        wsPort_(wsPort),
        allowDirectMediasoupDebug_(allowDirectMediasoupDebug),
        debugMode_(debugMode) {
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

        if (!featureEventBus_) {
            featureEventBus_ = eds::server_new::features::runtime::FeatureEventBus::instance();
        }
        if (!sessionAuthStore_) {
            sessionAuthStore_ = eds::server_new::auth::AuthServices::sessionStore();
        }
        if (!audioSessionLifecycleSubscription_.connected() && featureEventBus_) {
            audioSessionLifecycleSubscription_ =
                featureEventBus_->subscribe<eds::server_new::features::events::AudioSessionLifecycleEvent>(
                    [this](const auto& event) { onAudioSessionLifecycleEvent(event); });
        }

        running_.store(true);
        if (debugMode_) {
            std::cout << "[mediasoup][debug][signaling] gateway started on port " << wsPort_ << ".\n";
        }
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

        audioSessionLifecycleSubscription_.disconnect();
        ioContext_.stop();

        std::lock_guard<std::mutex> lock(peersMutex_);
        if (debugMode_) {
            std::cout << "[mediasoup][debug][signaling] gateway stopped. total_messages="
                << receivedMessages_.load()
                << ", total_forwarded_events="
                << forwardedEvents_.load()
                << ", active_peers="
                << peerToSession_.size()
                << "\n";
        }

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

        if (debugMode_) {
            std::cout << "[mediasoup][debug][signaling] connected session="
                << reinterpret_cast<std::uintptr_t>(session)
                << ", trustedPeer="
                << trustedPeer
                << "\n";
        }

        json assign{
            { "type", "peer_assigned" },
            { "peer", trustedPeer }
        };
        wsServer_->sendText(session, assign.dump());
    }

    void MediasoupSignalingGateway::onDisconnected(void* session) {
        std::string trustedPeer;
        {
            std::lock_guard<std::mutex> lock(peersMutex_);
            auto iterator = sessionToPeer_.find(session);
            if (iterator == sessionToPeer_.end()) {
                return;
            }

            trustedPeer = iterator->second;
            peerToSession_.erase(trustedPeer);
            sessionToPeer_.erase(iterator);
        }

        if (debugMode_) {
            std::cout << "[mediasoup][debug][signaling] disconnected session="
                << reinterpret_cast<std::uintptr_t>(session)
                << ", trustedPeer="
                << trustedPeer
                << "\n";
        }

        if (sessionAuthStore_) {
            sessionAuthStore_->unbind(reinterpret_cast<std::uintptr_t>(session));
        }

        app_.notifyFeatureSessionDisconnected(trustedPeer, reinterpret_cast<std::uintptr_t>(session));
    }

    void MediasoupSignalingGateway::onMessage(const std::string& text, void* session) {
        if (!wsServer_) {
            return;
        }

        const auto messageNo = receivedMessages_.fetch_add(1) + 1;
        json response;

        try {
            const auto trustedPeer = resolveTrustedPeer(session);
            const auto request = json::parse(text);

            if (!request.is_object()) {
                response = {
                    { "type", "dispatch_result" },
                    { "ok", false },
                    { "message", "request must be a JSON object." }
                };
                wsServer_->sendText(session, response.dump());
                return;
            }

            const auto objectType = request.value("object", std::string{});
            const auto agentType = request.value("agent", std::string{});
            const auto actionType = request.value("action", std::string{});
            const bool directMediasoupRequested = objectType == std::string(kRouteObject);

            if (debugMode_) {
                std::cout << "[mediasoup][debug][signaling] inbound#" << messageNo
                    << " session=" << reinterpret_cast<std::uintptr_t>(session)
                    << " peer=" << trustedPeer
                    << " object=" << objectType
                    << " agent=" << agentType
                    << " action=" << actionType
                    << " payload_bytes=" << text.size()
                    << "\n";
            }

            if (actionType.empty()) {
                response = {
                    { "type", "dispatch_result" },
                    { "ok", false },
                    { "message", "action must not be empty." }
                };
                wsServer_->sendText(session, response.dump());
                return;
            }

            if (objectType.empty()) {
                response = {
                    { "type", "dispatch_result" },
                    { "ok", false },
                    { "message", "object must not be empty." }
                };
                wsServer_->sendText(session, response.dump());
                return;
            }

            json context = request.value("ctx", json::object());
            if (!context.is_object()) {
                response = {
                    { "type", "dispatch_result" },
                    { "ok", false },
                    { "message", "ctx must be a JSON object." }
                };
                wsServer_->sendText(session, response.dump());
                return;
            }

            context["peer"] = trustedPeer;
            context["peerId"] = trustedPeer;

            const bool isAuthRequest =
                objectType == std::string(eds::server_new::features::auth::kAuthRouteObject) &&
                (actionType == std::string(eds::server_new::features::auth::kActionBindSession) ||
                    actionType == std::string(eds::server_new::features::auth::kActionRestoreSession) ||
                    actionType == std::string(eds::server_new::features::auth::kActionLogoutSession));

            if (isAuthRequest) {
                context["trustedPeer"] = trustedPeer;
            }
            else {
                if (!sessionAuthStore_) {
                    response = {
                        { "type", "dispatch_result" },
                        { "object", objectType },
                        { "agent", agentType },
                        { "action", actionType },
                        { "peer", trustedPeer },
                        { "ok", false },
                        { "message", "Session auth store is not configured." }
                    };
                    wsServer_->sendText(session, response.dump());
                    return;
                }

                const auto authState = sessionAuthStore_->get(reinterpret_cast<std::uintptr_t>(session));
                if (!authState.has_value() || !authState->authenticated) {
                    response = {
                        { "type", "dispatch_result" },
                        { "object", objectType },
                        { "agent", agentType },
                        { "action", actionType },
                        { "peer", trustedPeer },
                        { "ok", false },
                        { "message", "Authentication required. Call auth/session/bind_session first." }
                    };
                    wsServer_->sendText(session, response.dump());
                    return;
                }
            }

            if (directMediasoupRequested && !allowDirectMediasoupDebug_) {
                response = {
                    { "type", "dispatch_result" },
                    { "object", objectType },
                    { "agent", agentType },
                    { "action", actionType },
                    { "peer", trustedPeer },
                    { "ok", false },
                    { "message", "Direct mediasoup commands are disabled. This path is test-only; production flow must use feature orchestration." }
                };
                wsServer_->sendText(session, response.dump());
                return;
            }

            eds::server_new::features::runtime::FeatureDispatchRequest dispatchRequest;
            dispatchRequest.sessionHandle = reinterpret_cast<std::uintptr_t>(session);
            dispatchRequest.peerId = trustedPeer;
            dispatchRequest.objectType = objectType;
            dispatchRequest.agentType = agentType;
            dispatchRequest.actionType = actionType;
            dispatchRequest.context = std::move(context);

            auto dispatchResult = app_.dispatchFeature(dispatchRequest);
            const auto& status = dispatchResult.status;
            const auto effectiveAgent = dispatchResult.effectiveAgent.empty()
                ? agentType
                : dispatchResult.effectiveAgent;

            if (debugMode_) {
                std::cout << "[mediasoup][debug][signaling] dispatch_result"
                    << " peer=" << trustedPeer
                    << " object=" << objectType
                    << " agent=" << effectiveAgent
                    << " action=" << actionType
                    << " ok=" << (status.ok ? "true" : "false")
                    << " events=" << dispatchResult.outboundEvents.size()
                    << " message=\"" << status.message << "\""
                    << "\n";
            }

            response = {
                { "type", "dispatch_result" },
                { "object", objectType },
                { "agent", effectiveAgent },
                { "action", actionType },
                { "peer", trustedPeer },
                { "ok", status.ok },
                { "message", status.message }
            };

            if (isAuthRequest && sessionAuthStore_) {
                const auto authState = sessionAuthStore_->get(reinterpret_cast<std::uintptr_t>(session));
                if (authState.has_value()) {
                    response["authenticated"] = authState->authenticated;
                    response["userId"] = authState->userId;
                    response["email"] = authState->email;
                    response["deviceId"] = authState->deviceId;
                }
                else if (actionType == std::string(eds::server_new::features::auth::kActionLogoutSession)) {
                    response["authenticated"] = false;
                }
            }

            if (directMediasoupRequested) {
                response["note"] = "Direct mediasoup mode is enabled for isolated tests only.";
            }

            for (const auto& event : dispatchResult.outboundEvents) {
                auto outboundEvent = event;
                const auto eventType = outboundEvent.value("type", std::string("unknown"));
                const auto deliverIt = outboundEvent.find("deliverTo");

                if (deliverIt == outboundEvent.end()) {
                    wsServer_->sendText(session, outboundEvent.dump());
                    if (debugMode_) {
                        forwardedEvents_.fetch_add(1);
                        std::cout << "[mediasoup][debug][signaling] outbound_event"
                            << " target=current_peer"
                            << " event_type=" << eventType
                            << "\n";
                    }
                    continue;
                }

                if (deliverIt->is_null()) {
                    outboundEvent.erase("deliverTo");
                    wsServer_->sendText(session, outboundEvent.dump());
                    if (debugMode_) {
                        forwardedEvents_.fetch_add(1);
                        std::cout << "[mediasoup][debug][signaling] outbound_event"
                            << " target=current_peer"
                            << " event_type=" << eventType
                            << "\n";
                    }
                    continue;
                }

                const auto serializedEvent = [&outboundEvent]() {
                    auto payload = outboundEvent;
                    payload.erase("deliverTo");
                    return payload.dump();
                    }();

                if (deliverIt->is_string()) {
                    const auto targetPeer = deliverIt->get<std::string>();
                    const auto delivered = sendTextToPeer(targetPeer, serializedEvent);
                    if (debugMode_) {
                        if (delivered) {
                            forwardedEvents_.fetch_add(1);
                        }
                        std::cout << "[mediasoup][debug][signaling] outbound_event"
                            << " target=" << targetPeer
                            << " event_type=" << eventType
                            << " delivered=" << (delivered ? "true" : "false")
                            << "\n";
                    }
                    continue;
                }

                if (deliverIt->is_array()) {
                    for (const auto& recipient : *deliverIt) {
                        if (!recipient.is_string()) {
                            continue;
                        }

                        const auto targetPeer = recipient.get<std::string>();
                        const auto delivered = sendTextToPeer(targetPeer, serializedEvent);
                        if (debugMode_) {
                            if (delivered) {
                                forwardedEvents_.fetch_add(1);
                            }
                            std::cout << "[mediasoup][debug][signaling] outbound_event"
                                << " target=" << targetPeer
                                << " event_type=" << eventType
                                << " delivered=" << (delivered ? "true" : "false")
                                << "\n";
                        }
                    }
                    continue;
                }

                wsServer_->sendText(session, outboundEvent.dump());
                if (debugMode_) {
                    forwardedEvents_.fetch_add(1);
                    std::cout << "[mediasoup][debug][signaling] outbound_event"
                        << " target=current_peer"
                        << " event_type=" << eventType
                        << " note=invalid_deliverTo"
                        << "\n";
                }
            }
        }
        catch (const std::exception& ex) {
            response = {
                { "type", "dispatch_result" },
                { "ok", false },
                { "message", std::string("invalid request: ") + ex.what() }
            };
        }

        wsServer_->sendText(session, response.dump());
    }

    void MediasoupSignalingGateway::onAudioSessionLifecycleEvent(
        const eds::server_new::features::events::AudioSessionLifecycleEvent& event) {
        if (!running_.load() || !wsServer_) {
            return;
        }
        if (!event.started && !event.ended) {
            return;
        }

        std::unordered_set<std::string> recipients;
        for (const auto& peerId : event.notifyPeerIds) {
            if (!peerId.empty()) {
                recipients.insert(peerId);
            }
        }
        if (recipients.empty()) {
            return;
        }

        const json payload{
            { "type", "audio_session_lifecycle" },
            { "object", std::string(kRouteObject) },
            { "roomId", event.roomId },
            { "actorPeerId", event.actorPeerId },
            { "started", event.started },
            { "ended", event.ended },
            { "reason", event.reason },
            { "memberPeerIds", event.memberPeerIds }
        };

        const auto serialized = payload.dump();
        if (debugMode_) {
            std::cout << "[mediasoup][debug][signaling] audio_session_lifecycle"
                << " room=" << event.roomId
                << " actor=" << event.actorPeerId
                << " started=" << (event.started ? "true" : "false")
                << " ended=" << (event.ended ? "true" : "false")
                << " recipients=" << recipients.size()
                << "\n";
        }

        for (const auto& peerId : recipients) {
            const auto delivered = sendTextToPeer(peerId, serialized);
            if (debugMode_ && delivered) {
                forwardedEvents_.fetch_add(1);
            }
        }
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

    bool MediasoupSignalingGateway::sendTextToPeer(std::string_view peerId, const std::string& text) {
        if (!wsServer_ || peerId.empty()) {
            return false;
        }

        void* targetSession = nullptr;
        {
            std::lock_guard<std::mutex> lock(peersMutex_);
            auto peerIt = peerToSession_.find(std::string(peerId));
            if (peerIt == peerToSession_.end()) {
                if (debugMode_) {
                    std::cout << "[mediasoup][debug][signaling] sendTextToPeer skipped: target peer not found: "
                        << peerId
                        << "\n";
                }
                return false;
            }
            targetSession = peerIt->second;
        }

        const auto sent = wsServer_->sendText(targetSession, text);
        if (debugMode_ && !sent) {
            std::cout << "[mediasoup][debug][signaling] sendTextToPeer failed: target="
                << peerId
                << " session="
                << reinterpret_cast<std::uintptr_t>(targetSession)
                << "\n";
        }
        return sent;
    }

} // namespace eds::server_new::mediasoup::signaling