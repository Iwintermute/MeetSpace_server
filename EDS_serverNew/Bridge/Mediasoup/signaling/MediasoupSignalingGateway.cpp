#include "Bridge/Mediasoup/signaling/MediasoupSignalingGateway.h"

#include "App/ApplicationCore.h"
#include "Auth/runtime/AuthServices.h"
#include "infrastructure/control_plane/runtime/ControlPlaneServices.h"
#include "utils/WordGenerator.h"

#include <boost/asio/post.hpp>
#include <algorithm>

#include <chrono>
#include <iostream>
#include <nlohmann/json.hpp>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

namespace eds::server_new::mediasoup::signaling {
    using json = nlohmann::json;

    namespace {

        json makeDispatchFailureResponse(const std::string& message) {
            return json{
                { "type", "dispatch_result" },
                { "ok", false },
                { "message", message }
            };
        }

        bool shouldAttachData(const json& value) {
            if (value.is_null()) {
                return false;
            }

            if (value.is_object() || value.is_array()) {
                return !value.empty();
            }

            return true;
        }

        json extractContextObject(const json& request) {
            const auto ctxIt = request.find("ctx");
            if (ctxIt == request.end() || ctxIt->is_null()) {
                return json::object();
            }

            if (!ctxIt->is_object()) {
                throw std::runtime_error("ctx must be a JSON object.");
            }

            return *ctxIt;
        }

        constexpr std::size_t kOfflineOutboxBatchSize = 16;
        constexpr std::int32_t kOfflineOutboxMaxAttempts = 8;
        constexpr auto kOfflineOutboxIdleSleep = std::chrono::milliseconds(450);
        constexpr auto kOfflineOutboxBusySleep = std::chrono::milliseconds(120);

    } // namespace

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
        bool expected = false;
        if (!running_.compare_exchange_strong(expected, true)) {
            return true;
        }

        if (!ioContext_.init()) {
            running_.store(false);
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
            running_.store(false);
            return false;
        }

        if (!ioContext_.start()) {
            wsServer_->stop();
            wsServer_.reset();
            sendStrand_.reset();
            ioContext_.stop();
            running_.store(false);
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

        if (debugMode_) {
            std::cout << "[mediasoup][debug][signaling] gateway started on port "
                << wsPort_
                << ".\n";
        }

        startOfflineOutboxDispatcher();

        return true;
    }

    void MediasoupSignalingGateway::stop() {
        if (!running_.exchange(false)) {
            return;
        }
        stopOfflineOutboxDispatcher();

        audioSessionLifecycleSubscription_.disconnect();

        if (wsServer_) {
            try {
                wsServer_->stop();
            }
            catch (...) {
            }
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
        if (!running_.load() || session == nullptr) {
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
        if (session == nullptr) {
            return;
        }

        std::string peerId;

        {
            std::lock_guard<std::mutex> lock(peersMutex_);

            auto iterator = sessionToPeer_.find(session);
            if (iterator != sessionToPeer_.end()) {
                peerId = iterator->second;
                peerToSession_.erase(peerId);
                sessionToPeer_.erase(iterator);
            }
        }

        if (debugMode_) {
            std::cout << "[mediasoup][debug][signaling] disconnected session="
                << reinterpret_cast<std::uintptr_t>(session)
                << ", trustedPeer="
                << (peerId.empty() ? std::string("<unknown>") : peerId)
                << "\n";
        }

        try {
            auto disconnectEvents =
                app_.notifyFeatureSessionDisconnected(peerId, reinterpret_cast<std::uintptr_t>(session));

            for (const auto& event : disconnectEvents) {
                if (!event.is_object()) {
                    continue;
                }

                const auto deliverIt = event.find("deliverTo");
                if (deliverIt == event.end() || deliverIt->is_null()) {
                    continue;
                }

                auto payload = event;
                payload.erase("deliverTo");
                const auto serializedEvent = payload.dump();

                if (deliverIt->is_string()) {
                    const auto targetPeer = deliverIt->get<std::string>();
                    if (!targetPeer.empty()) {
                        postTextToPeer(targetPeer, serializedEvent);
                    }
                    continue;
                }

                if (deliverIt->is_array()) {
                    for (const auto& item : *deliverIt) {
                        if (!item.is_string()) {
                            continue;
                        }

                        const auto targetPeer = item.get<std::string>();
                        if (!targetPeer.empty()) {
                            postTextToPeer(targetPeer, serializedEvent);
                        }
                    }
                }
            }
        }
        catch (const std::exception& ex) {
            if (debugMode_) {
                std::cout << "[mediasoup][debug][signaling] notifyFeatureSessionDisconnected exception: "
                    << ex.what()
                    << "\n";
            }
        }
        catch (...) {
            if (debugMode_) {
                std::cout << "[mediasoup][debug][signaling] notifyFeatureSessionDisconnected unknown exception.\n";
            }
        }
    }

    void MediasoupSignalingGateway::onMessage(const std::string& text, void* session) {
        if (!running_.load() || !wsServer_ || !sendStrand_ || session == nullptr) {
            return;
        }

        const auto messageNo = receivedMessages_.fetch_add(1) + 1;

        try {
            const auto trustedPeer = resolveTrustedPeer(session);
            const auto request = json::parse(text);

            if (!request.is_object()) {
                postText(session, makeDispatchFailureResponse("request root must be a JSON object.").dump());
                return;
            }

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
                currentSessionMessages.push_back(makeDispatchFailureResponse(message).dump());
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

            if (directMediasoupRequested && !allowDirectMediasoupDebug_) {
                queueValidationFailure(
                    "Direct mediasoup control is disabled. Use feature-level orchestration actions.");
                postTexts(session, std::move(currentSessionMessages));
                return;
            }

            const auto context = extractContextObject(request);
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

            if (shouldAttachData(status.data)) {
                response["data"] = status.data;
            }

            if (status.ok && directMediasoupRequested && !status.message.empty()) {
                const auto backendPayload = json::parse(status.message, nullptr, false);
                if (backendPayload.is_object()) {
                    if (!response.contains("data") && backendPayload.contains("data")) {
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
                if (!event.is_object()) {
                    continue;
                }

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
                    if (!targetPeer.empty()) {
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

                if (deliverIt->is_array()) {
                    for (const auto& recipient : *deliverIt) {
                        if (!recipient.is_string()) {
                            continue;
                        }

                        const auto targetPeer = recipient.get<std::string>();
                        if (targetPeer.empty()) {
                            continue;
                        }

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
        catch (...) {
            json response{
                { "type", "dispatch_result" },
                { "ok", false },
                { "message", "invalid request: unknown exception." }
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
        while (peer.empty() || peerToSession_.find(peer) != peerToSession_.end()) {
            peer = utils::WordGenerator(12);
        }

        sessionToPeer_.emplace(session, peer);
        peerToSession_.emplace(peer, session);

        return peer;
    }

    void MediasoupSignalingGateway::postText(void* session, std::string text) {
        if (!session || text.empty() || !sendStrand_) {
            return;
        }

        boost::asio::post(*sendStrand_, [this, session, text = std::move(text)]() mutable {
            if (!running_.load() || !wsServer_) {
                return;
            }

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
                if (text.empty()) {
                    continue;
                }

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

    bool MediasoupSignalingGateway::isPeerConnected(std::string_view peerId) {
        if (peerId.empty()) {
            return false;
        }

        std::lock_guard<std::mutex> lock(peersMutex_);
        return peerToSession_.find(std::string(peerId)) != peerToSession_.end();
    }

    void MediasoupSignalingGateway::startOfflineOutboxDispatcher() {
        outboxDispatcherStop_.store(false, std::memory_order_release);
        if (outboxDispatcherThread_.joinable()) {
            return;
        }

        outboxDispatcherThread_ = std::thread([this]() {
            runOfflineOutboxDispatcher();
            });
    }

    void MediasoupSignalingGateway::stopOfflineOutboxDispatcher() {
        outboxDispatcherStop_.store(true, std::memory_order_release);
        if (outboxDispatcherThread_.joinable()) {
            outboxDispatcherThread_.join();
        }
    }

    void MediasoupSignalingGateway::runOfflineOutboxDispatcher() {
        while (!outboxDispatcherStop_.load(std::memory_order_acquire)) {
            if (!running_.load(std::memory_order_acquire)) {
                break;
            }

            auto repository = eds::server_new::control_plane::ControlPlaneServices::repository();
            if (!repository || !repository->isReady()) {
                std::this_thread::sleep_for(kOfflineOutboxIdleSleep);
                continue;
            }

            auto claimStatus = repository->claimPendingOfflineOutbox(kOfflineOutboxBatchSize);
            if (!claimStatus.ok) {
                if (debugMode_) {
                    std::cout << "[mediasoup][debug][signaling] offline_outbox_claim_failed: "
                        << claimStatus.message
                        << "\n";
                }
                std::this_thread::sleep_for(kOfflineOutboxIdleSleep);
                continue;
            }

            const auto eventsIt = claimStatus.data.find("events");
            if (eventsIt == claimStatus.data.end() || !eventsIt->is_array() || eventsIt->empty()) {
                std::this_thread::sleep_for(kOfflineOutboxIdleSleep);
                continue;
            }

            auto sessionStore = eds::server_new::auth::AuthServices::sessionStore();
            bool handledEvents = false;

            for (const auto& event : *eventsIt) {
                if (!event.is_object()) {
                    continue;
                }
                if (outboxDispatcherStop_.load(std::memory_order_acquire) || !running_.load(std::memory_order_acquire)) {
                    break;
                }

                const auto outboxId = event.value("id", static_cast<std::int64_t>(0));
                if (outboxId <= 0) {
                    continue;
                }

                auto payload = event.value("payload", json::object());
                if (!payload.is_object() && !payload.is_array()) {
                    payload = json{
                        { "type", "offline_outbox_payload" },
                        { "rawPayload", event["payload"] }
                    };
                }

                std::vector<std::string> targetPeers;
                const auto targetPeerId = event.value("targetPeerId", std::string{});
                if (!targetPeerId.empty()) {
                    targetPeers.push_back(targetPeerId);
                }

                const auto recipientUserId = event.value("recipientUserId", std::string{});
                if (!recipientUserId.empty() && sessionStore) {
                    auto userPeers = sessionStore->resolvePeersForUser(recipientUserId);
                    targetPeers.insert(targetPeers.end(), userPeers.begin(), userPeers.end());
                }

                targetPeers.erase(
                    std::remove_if(targetPeers.begin(), targetPeers.end(), [](const auto& peer) { return peer.empty(); }),
                    targetPeers.end());
                std::sort(targetPeers.begin(), targetPeers.end());
                targetPeers.erase(std::unique(targetPeers.begin(), targetPeers.end()), targetPeers.end());

                bool delivered = false;
                const auto payloadText = payload.dump();
                for (const auto& targetPeer : targetPeers) {
                    if (!isPeerConnected(targetPeer)) {
                        continue;
                    }

                    postTextToPeer(targetPeer, payloadText);
                    delivered = true;
                }

                if (delivered) {
                    const auto deliveredStatus = repository->markOfflineOutboxDelivered(outboxId);
                    if (!deliveredStatus.ok && debugMode_) {
                        std::cout << "[mediasoup][debug][signaling] offline_outbox_mark_delivered_failed id="
                            << outboxId
                            << " message=" << deliveredStatus.message
                            << "\n";
                    }
                }
                else {
                    const auto retryStatus = repository->markOfflineOutboxRetry(
                        outboxId,
                        "Recipient is offline.",
                        kOfflineOutboxMaxAttempts);
                    if (!retryStatus.ok && debugMode_) {
                        std::cout << "[mediasoup][debug][signaling] offline_outbox_mark_retry_failed id="
                            << outboxId
                            << " message=" << retryStatus.message
                            << "\n";
                    }
                }

                handledEvents = true;
            }

            std::this_thread::sleep_for(handledEvents ? kOfflineOutboxBusySleep : kOfflineOutboxIdleSleep);
        }
    }

} // namespace eds::server_new::mediasoup::signaling