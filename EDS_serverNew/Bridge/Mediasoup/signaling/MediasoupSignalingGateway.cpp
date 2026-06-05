#include "Bridge/Mediasoup/signaling/MediasoupSignalingGateway.h"

#include "App/ApplicationCore.h"
#include "Auth/runtime/AuthServices.h"
#include "infrastructure/control_plane/runtime/ControlPlaneServices.h"
#include "utils/WordGenerator.h"

#include <boost/asio/post.hpp>
#include <algorithm>

#include <chrono>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <nlohmann/json.hpp>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace eds::server_new::mediasoup::signaling {
    using json = nlohmann::json;

    namespace {
        struct InboundRequestEnvelope {
            int contractVersion = 1;
            std::string objectType;
            std::string agentType;
            std::string actionType;
            json context = json::object();
            std::string correlationId;
            std::string messageType;
        };

        bool shouldAttachData(const json& value) {
            if (value.is_null()) {
                return false;
            }

            if (value.is_object() || value.is_array()) {
                return !value.empty();
            }

            return true;
        }

        std::string toLowerCopy(std::string value) {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char symbol) {
                return static_cast<char>(std::tolower(symbol));
                });
            return value;
        }

        std::string deriveErrorCodeFromMessage(const std::string& message) {
            const auto lowered = toLowerCopy(message);
            if (lowered.find("unauthorized") != std::string::npos) {
                return "unauthorized";
            }
            if (lowered.find("forbidden") != std::string::npos
                || lowered.find("disabled") != std::string::npos) {
                return "forbidden";
            }
            if (lowered.find("not found") != std::string::npos) {
                return "not_found";
            }
            if (lowered.find("timeout") != std::string::npos) {
                return "timeout";
            }
            if (lowered.find("must not be empty") != std::string::npos
                || lowered.find("invalid") != std::string::npos
                || lowered.find("json") != std::string::npos) {
                return "validation_error";
            }
            return "dispatch_failed";
        }

        std::string readStringField(const json& source, std::initializer_list<const char*> names) {
            for (const auto* name : names) {
                if (name == nullptr || name[0] == '\0') {
                    continue;
                }

                const auto it = source.find(name);
                if (it == source.end() || it->is_null()) {
                    continue;
                }
                if (it->is_string()) {
                    return it->get<std::string>();
                }
                if (it->is_number_integer() || it->is_number_unsigned() || it->is_number_float() || it->is_boolean()) {
                    return it->dump();
                }
            }

            return {};
        }

        bool extractContextObject(const json& source, json& context, std::string& error) {
            for (const auto* name : { "ctx", "context" }) {
                const auto ctxIt = source.find(name);
                if (ctxIt == source.end() || ctxIt->is_null()) {
                    continue;
                }

                if (!ctxIt->is_object()) {
                    error = std::string(name) + " must be a JSON object.";
                    return false;
                }

                context = *ctxIt;
                return true;
            }

            context = json::object();
            error.clear();
            return true;
        }

        bool parseInboundRequestEnvelope(
            const json& request,
            InboundRequestEnvelope& envelope,
            std::string& error) {
            envelope = InboundRequestEnvelope{};
            error.clear();

            if (!request.is_object()) {
                error = "request root must be a JSON object.";
                return false;
            }

            const auto legacyType = readStringField(request, { "type", "messageType", "message_type" });
            const auto kind = readStringField(request, { "kind" });
            envelope.messageType = legacyType.empty() ? kind : legacyType;

            if (const auto versionIt = request.find("v");
                versionIt != request.end() && versionIt->is_number_integer()) {
                envelope.contractVersion = std::max(1, versionIt->get<int>());
            }
            else if (const auto versionText = readStringField(request, { "version" }); !versionText.empty()) {
                try {
                    envelope.contractVersion = std::max(1, std::stoi(versionText));
                }
                catch (...) {
                    envelope.contractVersion = 1;
                }
            }

            const json* commandNode = &request;
            if (const auto requestNode = request.find("request");
                requestNode != request.end() && requestNode->is_object()) {
                commandNode = &(*requestNode);
                envelope.contractVersion = std::max(2, envelope.contractVersion);
            }

            envelope.objectType = readStringField(*commandNode, { "object", "obj", "feature" });
            envelope.agentType = readStringField(*commandNode, { "agent", "module" });
            envelope.actionType = readStringField(*commandNode, { "action", "operation", "op" });

            if (envelope.objectType.empty()) {
                envelope.objectType = readStringField(request, { "object", "obj", "feature" });
            }
            if (envelope.agentType.empty()) {
                envelope.agentType = readStringField(request, { "agent", "module" });
            }
            if (envelope.actionType.empty()) {
                envelope.actionType = readStringField(request, { "action", "operation", "op" });
            }

            if (!extractContextObject(*commandNode, envelope.context, error)) {
                return false;
            }
            if (envelope.context.empty()) {
                if (!extractContextObject(request, envelope.context, error)) {
                    return false;
                }
            }

            envelope.correlationId = readStringField(
                envelope.context,
                { "correlationId", "clientRequestId", "requestId" });

            auto requestId = readStringField(*commandNode, { "id", "requestId" });
            if (requestId.empty()) {
                requestId = readStringField(request, { "requestId", "id" });
            }
            if (envelope.correlationId.empty()) {
                envelope.correlationId = requestId;
            }

            if (!envelope.correlationId.empty()) {
                envelope.context["correlationId"] = envelope.correlationId;
                envelope.context["clientRequestId"] = envelope.correlationId;
                envelope.context["requestId"] = envelope.correlationId;
            }

            return true;
        }

        void applyContractV2Response(
            json& response,
            int contractVersion,
            const std::string& correlationId,
            bool ok,
            const std::string& message,
            const json& data,
            const json& errorDetails,
            std::string errorCode = {}) {
            if (contractVersion < 2) {
                return;
            }

            response["v"] = 2;
            response["kind"] = "response";
            if (!correlationId.empty()) {
                response["requestId"] = correlationId;
            }

            json result{
                { "ok", ok },
                { "message", message }
            };
            if (shouldAttachData(data)) {
                result["data"] = data;
            }
            response["result"] = std::move(result);

            if (!ok) {
                if (errorCode.empty()) {
                    errorCode = deriveErrorCodeFromMessage(message);
                }

                json error{
                    { "code", std::move(errorCode) },
                    { "message", message }
                };
                if (shouldAttachData(errorDetails)) {
                    error["details"] = errorDetails;
                }
                response["error"] = std::move(error);
            }
        }

        json makeDispatchFailureResponse(
            const std::string& message,
            std::string errorCode = "validation_error",
            int contractVersion = 1,
            const std::string& correlationId = {},
            const std::string& objectType = {},
            const std::string& agentType = {},
            const std::string& actionType = {},
            json errorDetails = json::object()) {
            json response{
                { "type", "dispatch_result" },
                { "ok", false },
                { "message", message }
            };

            if (!objectType.empty()) {
                response["object"] = objectType;
            }
            if (!agentType.empty()) {
                response["agent"] = agentType;
            }
            if (!actionType.empty()) {
                response["action"] = actionType;
            }
            if (!correlationId.empty()) {
                response["correlationId"] = correlationId;
                response["clientRequestId"] = correlationId;
            }

            applyContractV2Response(
                response,
                contractVersion,
                correlationId,
                false,
                message,
                json::object(),
                std::move(errorDetails),
                std::move(errorCode));
            return response;
        }

        std::string readFirstEnvValue(std::initializer_list<const char*> names) {
            for (const auto* name : names) {
                if (name == nullptr || name[0] == '\0') {
                    continue;
                }
                const auto* value = std::getenv(name);
                if (value == nullptr || value[0] == '\0') {
                    continue;
                }
                return std::string(value);
            }
            return {};
        }

        std::size_t readBoundedSizeTEnv(
            std::initializer_list<const char*> names,
            std::size_t fallbackValue,
            std::size_t minValue,
            std::size_t maxValue) {
            auto value = fallbackValue;
            const auto raw = readFirstEnvValue(names);
            if (!raw.empty()) {
                try {
                    value = static_cast<std::size_t>(std::stoull(raw));
                }
                catch (...) {
                    value = fallbackValue;
                }
            }
            if (value < minValue) {
                return minValue;
            }
            if (value > maxValue) {
                return maxValue;
            }
            return value;
        }

        std::uint64_t readBoundedUint64Env(
            std::initializer_list<const char*> names,
            std::uint64_t fallbackValue,
            std::uint64_t minValue,
            std::uint64_t maxValue) {
            auto value = fallbackValue;
            const auto raw = readFirstEnvValue(names);
            if (!raw.empty()) {
                try {
                    value = static_cast<std::uint64_t>(std::stoull(raw));
                }
                catch (...) {
                    value = fallbackValue;
                }
            }
            if (value < minValue) {
                return minValue;
            }
            if (value > maxValue) {
                return maxValue;
            }
            return value;
        }

        unsigned int readBoundedUnsignedIntEnv(
            std::initializer_list<const char*> names,
            unsigned int fallbackValue,
            unsigned int minValue,
            unsigned int maxValue) {
            auto value = fallbackValue;
            const auto raw = readFirstEnvValue(names);
            if (!raw.empty()) {
                try {
                    value = static_cast<unsigned int>(std::stoul(raw));
                }
                catch (...) {
                    value = fallbackValue;
                }
            }
            if (value < minValue) {
                return minValue;
            }
            if (value > maxValue) {
                return maxValue;
            }
            return value;
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
        bool debugMode,
        transport::WebSocketTlsOptions tlsOptions)
        : app_(app),
        wsPort_(wsPort),
        allowDirectMediasoupDebug_(allowDirectMediasoupDebug),
        debugMode_(debugMode),
        tlsOptions_(std::move(tlsOptions)) {
        loadRuntimeLimitsFromEnvironment();
    }

    MediasoupSignalingGateway::~MediasoupSignalingGateway() {
        stop();
    }

    void MediasoupSignalingGateway::loadRuntimeLimitsFromEnvironment() {
        maxConcurrentConnections_ = readBoundedSizeTEnv(
            {
                "MEETSPACE_SIGNALING_MAX_CONNECTIONS",
                "EDUSPACE_SIGNALING_MAX_CONNECTIONS",
                "MEDIASOUP_SIGNALING_MAX_CONNECTIONS"
            },
            5000,
            64,
            200000);
        maxMessagesPerSecondPerPeer_ = readBoundedUint64Env(
            {
                "MEETSPACE_SIGNALING_MAX_MESSAGES_PER_SECOND_PER_PEER",
                "EDUSPACE_SIGNALING_MAX_MESSAGES_PER_SECOND_PER_PEER",
                "MEDIASOUP_SIGNALING_MAX_MESSAGES_PER_SECOND_PER_PEER"
            },
            100,
            10,
            100000);
        ioThreadCount_ = readBoundedUnsignedIntEnv(
            {
                "MEETSPACE_SIGNALING_IO_THREADS",
                "EDUSPACE_SIGNALING_IO_THREADS",
                "MEDIASOUP_SIGNALING_IO_THREADS"
            },
            0,
            0,
            256);
    }

    bool MediasoupSignalingGateway::start() {
        bool expected = false;
        if (!running_.compare_exchange_strong(expected, true)) {
            return true;
        }

        if (!ioContext_.init(ioThreadCount_)) {
            running_.store(false);
            return false;
        }

        sendStrand_ = std::make_unique<SendStrand>(boost::asio::make_strand(ioContext_.io()));

        wsServer_ = std::make_unique<transport::WebSocketServer>(ioContext_.io(), wsPort_, tlsOptions_);
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
            std::cout << "[mediasoup][debug][signaling] gateway started on "
                << (tlsOptions_.enabled ? "wss://" : "ws://")
                << "0.0.0.0:"
                << wsPort_
                << ", max_connections="
                << maxConcurrentConnections_
                << ", max_messages_per_second_per_peer="
                << maxMessagesPerSecondPerPeer_
                << ", io_threads="
                << (ioThreadCount_ == 0 ? std::string("auto") : std::to_string(ioThreadCount_))
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
        peerRateState_.clear();
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
        bool rejectConnection = false;

        {
            std::lock_guard<std::mutex> lock(peersMutex_);
            if (sessionToPeer_.size() >= maxConcurrentConnections_) {
                rejectConnection = true;
            }
        }

        if (rejectConnection) {
            if (debugMode_) {
                std::cout << "[mediasoup][debug][signaling] connection rejected: max "
                    << maxConcurrentConnections_
                    << " concurrent connections reached.\n";
            }
            if (wsServer_) {
                const auto rejectionPayload = makeDispatchFailureResponse(
                    "Signaling capacity reached. Please retry shortly.",
                    "capacity_exceeded");
                wsServer_->sendText(session, rejectionPayload.dump());
                wsServer_->closeSession(session);
            }
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
            peerRateState_.erase(session);
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
                std::vector<std::string> targetPeers;

                if (deliverIt->is_string()) {
                    const auto targetPeer = deliverIt->get<std::string>();
                    if (!targetPeer.empty()) {
                        targetPeers.push_back(targetPeer);
                    }
                }
                else if (deliverIt->is_array()) {
                    for (const auto& item : *deliverIt) {
                        if (!item.is_string()) {
                            continue;
                        }

                        const auto targetPeer = item.get<std::string>();
                        if (!targetPeer.empty()) {
                            targetPeers.push_back(targetPeer);
                        }
                    }
                }

                if (!targetPeers.empty()) {
                    postTextToPeers(std::move(targetPeers), serializedEvent);
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

        bool rejectForCapacity = false;
        bool rejectForRateLimit = false;
        {
            std::lock_guard<std::mutex> lock(peersMutex_);
            const auto knownSession = sessionToPeer_.find(session) != sessionToPeer_.end();
            if (!knownSession && sessionToPeer_.size() >= maxConcurrentConnections_) {
                rejectForCapacity = true;
            }
            else {
                auto& rate = peerRateState_[session];
                const auto now = std::chrono::steady_clock::now();
                if (now - rate.windowStart >= std::chrono::seconds(1)) {
                    rate.messageCount = 0;
                    rate.windowStart = now;
                }
                ++rate.messageCount;
                if (rate.messageCount > maxMessagesPerSecondPerPeer_) {
                    rejectForRateLimit = true;
                }
            }
        }

        if (rejectForCapacity) {
            postText(
                session,
                makeDispatchFailureResponse(
                    "Signaling capacity reached. Please retry shortly.",
                    "capacity_exceeded").dump());
            wsServer_->closeSession(session);
            return;
        }

        if (rejectForRateLimit) {
            postText(
                session,
                makeDispatchFailureResponse(
                    "Peer signaling rate limit exceeded.",
                    "rate_limited").dump());
            return;
        }

        const auto messageNo = receivedMessages_.fetch_add(1) + 1;

        try {
            const auto trustedPeer = resolveTrustedPeer(session);
            const auto request = json::parse(text);
            InboundRequestEnvelope inbound;
            std::string parseError;
            if (!parseInboundRequestEnvelope(request, inbound, parseError)) {
                postText(
                    session,
                    makeDispatchFailureResponse(
                        parseError,
                        "invalid_request",
                        inbound.contractVersion).dump());
                return;
            }

            const auto& messageType = inbound.messageType;
            const auto& objectType = inbound.objectType;
            const auto& agentType = inbound.agentType;
            const auto& actionType = inbound.actionType;
            const auto& correlationId = inbound.correlationId;
            const auto contractVersion = inbound.contractVersion;
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
            std::unordered_map<std::string, std::vector<std::string>> peerMessagesByPayload;
            auto queueValidationFailure = [&](const std::string& message, const std::string& code) {
                currentSessionMessages.push_back(
                    makeDispatchFailureResponse(
                        message,
                        code,
                        contractVersion,
                        correlationId,
                        objectType,
                        agentType,
                        actionType).dump());
                };

            if (messageType == "audio_data") {
                queueValidationFailure(
                    "audio_data over signaling websocket is forbidden. Use mediasoup WebRTC transport for media flow.",
                    "forbidden");
                postTexts(session, std::move(currentSessionMessages));
                return;
            }

            if (actionType.empty()) {
                queueValidationFailure("action must not be empty.", "validation_error");
                postTexts(session, std::move(currentSessionMessages));
                return;
            }

            if (objectType.empty()) {
                queueValidationFailure("object must not be empty.", "validation_error");
                postTexts(session, std::move(currentSessionMessages));
                return;
            }

            if (directMediasoupRequested && !allowDirectMediasoupDebug_) {
                queueValidationFailure(
                    "Direct mediasoup control is disabled. Use feature-level orchestration actions.",
                    "forbidden");
                postTexts(session, std::move(currentSessionMessages));
                return;
            }

            eds::server_new::features::runtime::FeatureDispatchRequest dispatchRequest;
            dispatchRequest.sessionHandle = reinterpret_cast<std::uintptr_t>(session);
            dispatchRequest.peerId = trustedPeer;
            dispatchRequest.objectType = objectType;
            dispatchRequest.agentType = agentType;
            dispatchRequest.actionType = actionType;
            dispatchRequest.context = inbound.context;

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

            json v2Data = json::object();
            if (response.contains("data")) {
                v2Data = response["data"];
            }
            applyContractV2Response(
                response,
                contractVersion,
                correlationId,
                status.ok,
                response.value("message", status.message),
                v2Data,
                status.ok ? json::object() : status.data);

            for (const auto& event : dispatchResult.outboundEvents) {
                if (!event.is_object()) {
                    continue;
                }

                auto outboundEvent = event;
                if (contractVersion >= 2) {
                    outboundEvent["v"] = 2;
                    outboundEvent["kind"] = "event";
                }
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
                        peerMessagesByPayload[serializedEvent].push_back(targetPeer);

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

                        peerMessagesByPayload[serializedEvent].push_back(targetPeer);

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

            for (auto& [payloadText, targetPeers] : peerMessagesByPayload) {
                postTextToPeers(std::move(targetPeers), std::move(payloadText));
            }
        }
        catch (const std::exception& ex) {
            postText(
                session,
                makeDispatchFailureResponse(
                    std::string("invalid request: ") + ex.what(),
                    "invalid_request").dump());
        }
        catch (...) {
            postText(
                session,
                makeDispatchFailureResponse(
                    "invalid request: unknown exception.",
                    "invalid_request").dump());
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

        std::vector<std::string> recipients;
        for (const auto& peerId : event.notifyPeerIds) {
            if (!peerId.empty()) {
                recipients.push_back(peerId);
            }
        }

        std::sort(recipients.begin(), recipients.end());
        recipients.erase(std::unique(recipients.begin(), recipients.end()), recipients.end());

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

        postTextToPeers(std::move(recipients), serialized);
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
        if (peerId.empty() || text.empty()) {
            return;
        }

        std::vector<std::string> peerIds;
        peerIds.push_back(std::move(peerId));
        postTextToPeers(std::move(peerIds), std::move(text));
    }

    void MediasoupSignalingGateway::postTextToPeers(std::vector<std::string> peerIds, std::string text) {
        if (peerIds.empty() || text.empty() || !sendStrand_) {
            return;
        }

        boost::asio::post(*sendStrand_, [this, peerIds = std::move(peerIds), text = std::move(text)]() mutable {
            if (!running_.load() || !wsServer_) {
                return;
            }

            peerIds.erase(
                std::remove_if(peerIds.begin(), peerIds.end(), [](const auto& peerId) { return peerId.empty(); }),
                peerIds.end());
            if (peerIds.empty()) {
                return;
            }

            std::sort(peerIds.begin(), peerIds.end());
            peerIds.erase(std::unique(peerIds.begin(), peerIds.end()), peerIds.end());

            std::vector<void*> targetSessions;
            targetSessions.reserve(peerIds.size());
            {
                std::lock_guard<std::mutex> lock(peersMutex_);
                for (const auto& peerId : peerIds) {
                    auto peerIt = peerToSession_.find(peerId);
                    if (peerIt == peerToSession_.end()) {
                        if (debugMode_) {
                            std::cout << "[mediasoup][debug][signaling] sendTextToPeers skipped: target peer not found: "
                                << peerId
                                << "\n";
                        }
                        continue;
                    }

                    targetSessions.push_back(peerIt->second);
                }
            }

            if (targetSessions.empty()) {
                return;
            }

            std::sort(targetSessions.begin(), targetSessions.end());
            targetSessions.erase(std::unique(targetSessions.begin(), targetSessions.end()), targetSessions.end());

            try {
                const auto sentCount = wsServer_->sendTexts(targetSessions, text);

                if (sentCount > 0) {
                    forwardedEvents_.fetch_add(sentCount);
                }
                else if (debugMode_) {
                    std::cout << "[mediasoup][debug][signaling] sendTextToPeers failed: no active target sessions\n";
                }
            }
            catch (const std::exception& ex) {
                if (debugMode_) {
                    std::cout << "[mediasoup][debug][signaling] sendTextToPeers exception: error="
                        << ex.what()
                        << "\n";
                }
            }
            catch (...) {
                if (debugMode_) {
                    std::cout << "[mediasoup][debug][signaling] sendTextToPeers unknown exception\n";
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

                const auto payloadText = payload.dump();
                std::vector<std::string> connectedPeers;
                connectedPeers.reserve(targetPeers.size());
                for (const auto& targetPeer : targetPeers) {
                    if (!isPeerConnected(targetPeer)) {
                        continue;
                    }
                    connectedPeers.push_back(targetPeer);
                }
                const bool delivered = !connectedPeers.empty();

                if (delivered) {
                    postTextToPeers(std::move(connectedPeers), payloadText);
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
