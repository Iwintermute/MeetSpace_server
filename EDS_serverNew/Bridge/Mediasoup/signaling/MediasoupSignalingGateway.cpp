#include "Bridge/Mediasoup/signaling/MediasoupSignalingGateway.h"

#include "App/ApplicationCore.h"
#include "utils/WordGenerator.h"

#include <boost/asio/post.hpp>

#include <chrono>
#include <iostream>
#include <nlohmann/json.hpp>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

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

        sendStrand_ = std::make_unique<SendStrand>(boost::asio::make_strand(ioContext_.io()));

        wsServer_ = std::make_unique<transport::WebSocketServer>(ioContext_.io(), wsPort_);
        wsServer_->setOnConnected([this](void* session) { onConnected(session); });
        wsServer_->setOnDisconnected([this](void* session) { onDisconnected(session); });
        wsServer_->setOnMessage([this](const std::string& text, void* session) { onMessage(text, session); });

        if (!wsServer_->start()) {
            wsServer_.reset();
            sendStrand_.reset();
            ioContext_.stop();
            return false;
        }

        if (!ioContext_.start()) {
            wsServer_->stop();
            wsServer_.reset();
            sendStrand_.reset();
            ioContext_.stop();
            return false;
        }

        if (!featureEventBus_) {
            featureEventBus_ = eds::server_new::features::runtime::FeatureEventBus::instance();
        }

        if (!audioSessionLifecycleSubscription_.connected() && featureEventBus_) {
            audioSessionLifecycleSubscription_ =
                featureEventBus_->subscribe<eds::server_new::features::events::AudioSessionLifecycleEvent>(
                    [this](const auto& event) { onAudioSessionLifecycleEvent(event); });
        }

        running_.store(true);

        if (debugMode_) {
            std::cout << "[mediasoup][debug][signaling] gateway started on port "
                << wsPort_
                << ".\n";
        }

        return true;
    }

    void MediasoupSignalingGateway::stop() {
        if (!running_.exchange(false)) {
            return;
        }

        audioSessionLifecycleSubscription_.disconnect();

        if (wsServer_) {
            wsServer_->stop();
            wsServer_.reset();
        }

        sendStrand_.reset();
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
        if (!session) {
            return;
        }

        const auto trustedPeer = resolveTrustedPeer(session);

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

        postText(session, assign.dump());
    }

    void MediasoupSignalingGateway::onDisconnected(void* session) {
        std::string peerId;

        {
            std::lock_guard<std::mutex> lock(peersMutex_);
            auto iterator = sessionToPeer_.find(session);
            if (iterator == sessionToPeer_.end()) {
                return;
            }

            peerId = iterator->second;

            if (debugMode_) {
                std::cout << "[mediasoup][debug][signaling] disconnected session="
                    << reinterpret_cast<std::uintptr_t>(session)
                    << ", trustedPeer="
                    << peerId
                    << "\n";
            }

            peerToSession_.erase(peerId);
            sessionToPeer_.erase(iterator);
        }

        app_.notifyFeatureSessionDisconnected(peerId, reinterpret_cast<std::uintptr_t>(session));
    }

    void MediasoupSignalingGateway::onMessage(const std::string& text, void* session) {
        if (!running_.load() || !wsServer_ || !sendStrand_ || session == nullptr) {
            return;
        }

        const auto messageNo = receivedMessages_.fetch_add(1) + 1;

        try {
            const auto trustedPeer = resolveTrustedPeer(session);
            const auto request = json::parse(text);

            const auto messageType = request.value("type", std::string{});
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

            std::vector<std::string> currentSessionMessages;
            std::vector<std::pair<std::string, std::string>> peerMessages;

            auto queueValidationFailure = [&currentSessionMessages](const std::string& message) {
                json response{
                    { "type", "dispatch_result" },
                    { "ok", false },
                    { "message", message }
                };
                currentSessionMessages.push_back(response.dump());
                };

            if (messageType == "audio_data") {
                queueValidationFailure(
                    "audio_data over signaling websocket is forbidden. Use mediasoup WebRTC transport for media flow.");
                postTexts(session, std::move(currentSessionMessages));
                return;
            }

            if (actionType.empty()) {
                queueValidationFailure("action must not be empty.");
                postTexts(session, std::move(currentSessionMessages));
                return;
            }

            if (objectType.empty()) {
                queueValidationFailure("object must not be empty.");
                postTexts(session, std::move(currentSessionMessages));
                return;
            }

            const auto context = request.value("ctx", json::object());
            const auto correlationId = context.value(
                "correlationId",
                context.value("clientRequestId", std::string{}));

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

            json response{
                { "type", "dispatch_result" },
                { "object", objectType },
                { "agent", effectiveAgent },
                { "action", actionType },
                { "peer", trustedPeer },
                { "ok", status.ok },
                { "message", status.message }
            };

            if (!correlationId.empty()) {
                response["correlationId"] = correlationId;
                response["clientRequestId"] = correlationId;
            }

            if (status.ok && objectType == std::string(kRouteObject) && !status.message.empty()) {
                const auto backendPayload = json::parse(status.message, nullptr, false);
                if (backendPayload.is_object()) {
                    if (backendPayload.contains("data")) {
                        response["data"] = backendPayload["data"];
                    }
                    if (backendPayload.contains("backend")) {
                        response["backend"] = backendPayload["backend"];
                    }
                    if (backendPayload.contains("signalingEvents")) {
                        response["signalingEvents"] = backendPayload["signalingEvents"];
                    }

                    const auto backendMessage = backendPayload.value("message", std::string{});
                    if (!backendMessage.empty()) {
                        response["message"] = backendMessage;
                    }
                }
            }

            if (directMediasoupRequested) {
                response["note"] = "Direct mediasoup mode is enabled for isolated tests only.";
            }

            for (const auto& event : dispatchResult.outboundEvents) {
                auto outboundEvent = event;
                const auto eventType = outboundEvent.value("type", std::string("unknown"));
                const auto deliverIt = outboundEvent.find("deliverTo");

                if (deliverIt == outboundEvent.end() || deliverIt->is_null()) {
                    outboundEvent.erase("deliverTo");
                    currentSessionMessages.push_back(outboundEvent.dump());

                    if (debugMode_) {
                        std::cout << "[mediasoup][debug][signaling] outbound_event"
                            << " target=current_peer"
                            << " event_type=" << eventType
                            << "\n";
                    }

                    continue;
                }

                auto payload = outboundEvent;
                payload.erase("deliverTo");
                const auto serializedEvent = payload.dump();

                if (deliverIt->is_string()) {
                    const auto targetPeer = deliverIt->get<std::string>();
                    peerMessages.emplace_back(targetPeer, serializedEvent);

                    if (debugMode_) {
                        std::cout << "[mediasoup][debug][signaling] outbound_event"
                            << " target=" << targetPeer
                            << " event_type=" << eventType
                            << " scheduled=true"
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
                        peerMessages.emplace_back(targetPeer, serializedEvent);

                        if (debugMode_) {
                            std::cout << "[mediasoup][debug][signaling] outbound_event"
                                << " target=" << targetPeer
                                << " event_type=" << eventType
                                << " scheduled=true"
                                << "\n";
                        }
                    }

                    continue;
                }

                outboundEvent.erase("deliverTo");
                currentSessionMessages.push_back(outboundEvent.dump());

                if (debugMode_) {
                    std::cout << "[mediasoup][debug][signaling] outbound_event"
                        << " target=current_peer"
                        << " event_type=" << eventType
                        << " note=invalid_deliverTo"
                        << "\n";
                }
            }

            currentSessionMessages.push_back(response.dump());
            postTexts(session, std::move(currentSessionMessages));

            for (auto& item : peerMessages) {
                postTextToPeer(std::move(item.first), std::move(item.second));
            }
        }
        catch (const std::exception& ex) {
            json response{
                { "type", "dispatch_result" },
                { "ok", false },
                { "message", std::string("invalid request: ") + ex.what() }
            };

            postText(session, response.dump());
        }
    }

    void MediasoupSignalingGateway::onAudioSessionLifecycleEvent(
        const eds::server_new::features::events::AudioSessionLifecycleEvent& event) {
        if (!running_.load() || !wsServer_ || !sendStrand_) {
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
            postTextToPeer(peerId, serialized);
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

    void MediasoupSignalingGateway::postText(void* session, std::string text) {
        if (!session) {
            return;
        }

        boost::asio::post(ioContext_.io(), [this, session, text = std::move(text)]() mutable {
            if (!running_.load() || !wsServer_) {
                return;
            }

            try {
                wsServer_->sendText(session, text);
            }
            catch (...) {
            }
            });
    }

    void MediasoupSignalingGateway::postTexts(void* session, std::vector<std::string> texts) {
        if (!session || texts.empty() || !sendStrand_) {
            return;
        }

        boost::asio::post(*sendStrand_, [this, session, texts = std::move(texts)]() mutable {
            if (!running_.load() || !wsServer_) {
                return;
            }

            for (const auto& text : texts) {
                try {
                    const auto sent = wsServer_->sendText(session, text);

                    if (sent) {
                        forwardedEvents_.fetch_add(1);
                    }
                    else if (debugMode_) {
                        std::cout << "[mediasoup][debug][signaling] sendText failed: session="
                            << reinterpret_cast<std::uintptr_t>(session)
                            << "\n";
                    }
                }
                catch (const std::exception& ex) {
                    if (debugMode_) {
                        std::cout << "[mediasoup][debug][signaling] sendText exception: session="
                            << reinterpret_cast<std::uintptr_t>(session)
                            << " error=" << ex.what()
                            << "\n";
                    }
                }
                catch (...) {
                    if (debugMode_) {
                        std::cout << "[mediasoup][debug][signaling] sendText unknown exception: session="
                            << reinterpret_cast<std::uintptr_t>(session)
                            << "\n";
                    }
                }
            }
            });
    }

    void MediasoupSignalingGateway::postTextToPeer(std::string peerId, std::string text) {
        if (peerId.empty() || text.empty() || !sendStrand_) {
            return;
        }

        boost::asio::post(*sendStrand_, [this, peerId = std::move(peerId), text = std::move(text)]() mutable {
            if (!running_.load() || !wsServer_) {
                return;
            }

            void* targetSession = nullptr;
            {
                std::lock_guard<std::mutex> lock(peersMutex_);
                auto peerIt = peerToSession_.find(peerId);
                if (peerIt == peerToSession_.end()) {
                    if (debugMode_) {
                        std::cout << "[mediasoup][debug][signaling] sendTextToPeer skipped: target peer not found: "
                            << peerId
                            << "\n";
                    }
                    return;
                }

                targetSession = peerIt->second;
            }

            try {
                const auto sent = wsServer_->sendText(targetSession, text);

                if (sent) {
                    forwardedEvents_.fetch_add(1);
                }
                else if (debugMode_) {
                    std::cout << "[mediasoup][debug][signaling] sendTextToPeer failed: target="
                        << peerId
                        << " session="
                        << reinterpret_cast<std::uintptr_t>(targetSession)
                        << "\n";
                }
            }
            catch (const std::exception& ex) {
                if (debugMode_) {
                    std::cout << "[mediasoup][debug][signaling] sendTextToPeer exception: target="
                        << peerId
                        << " error=" << ex.what()
                        << "\n";
                }
            }
            catch (...) {
                if (debugMode_) {
                    std::cout << "[mediasoup][debug][signaling] sendTextToPeer unknown exception: target="
                        << peerId
                        << "\n";
                }
            }
            });
    }

} // namespace eds::server_new::mediasoup::signaling