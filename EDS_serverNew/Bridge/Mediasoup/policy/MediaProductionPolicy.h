#pragma once

#include "Bridge/Mediasoup/service/MediaTransportTypes.h"
#include "contracts/Primitives.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <deque>
#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_map>

namespace eds::server_new::mediasoup::policy {

    struct MediaProductionPolicyConfig {
        int maxIdentifierLength = 128;
        int maxSdpBytes = 64 * 1024;
        int maxCandidateBytes = 16 * 1024;
        int maxJsonPayloadBytes = 256 * 1024;
        int maxActionsPerWindow = 240;
        int actionWindowSeconds = 10;
        int backendConnectTimeoutMs = 5000;
        int backendOperationTimeoutMs = 8000;
        int backendMaxRetries = 1;
        bool allowTestRtpInjection = false;
        bool enforceTrackTypeValidation = true;
        bool enforceKindValidation = true;
    };

    class MediaProductionPolicy final {
    public:
        static MediaProductionPolicyConfig fromEnvironment() {
            MediaProductionPolicyConfig config;
            config.maxIdentifierLength = readIntEnv(
                { "MEETSPACE_MEDIA_POLICY_MAX_IDENTIFIER_LENGTH", "EDUSPACE_MEDIA_POLICY_MAX_IDENTIFIER_LENGTH" },
                config.maxIdentifierLength);
            config.maxSdpBytes = readIntEnv(
                { "MEETSPACE_MEDIA_POLICY_MAX_SDP_BYTES", "EDUSPACE_MEDIA_POLICY_MAX_SDP_BYTES" },
                config.maxSdpBytes);
            config.maxCandidateBytes = readIntEnv(
                { "MEETSPACE_MEDIA_POLICY_MAX_CANDIDATE_BYTES", "EDUSPACE_MEDIA_POLICY_MAX_CANDIDATE_BYTES" },
                config.maxCandidateBytes);
            config.maxJsonPayloadBytes = readIntEnv(
                { "MEETSPACE_MEDIA_POLICY_MAX_JSON_PAYLOAD_BYTES", "EDUSPACE_MEDIA_POLICY_MAX_JSON_PAYLOAD_BYTES" },
                config.maxJsonPayloadBytes);
            config.maxActionsPerWindow = readIntEnv(
                { "MEETSPACE_MEDIA_POLICY_MAX_ACTIONS_PER_WINDOW", "EDUSPACE_MEDIA_POLICY_MAX_ACTIONS_PER_WINDOW" },
                config.maxActionsPerWindow);
            config.actionWindowSeconds = readIntEnv(
                { "MEETSPACE_MEDIA_POLICY_ACTION_WINDOW_SECONDS", "EDUSPACE_MEDIA_POLICY_ACTION_WINDOW_SECONDS" },
                config.actionWindowSeconds);
            config.backendConnectTimeoutMs = readIntEnv(
                {
                    "MEETSPACE_MEDIA_POLICY_BACKEND_CONNECT_TIMEOUT_MS",
                    "EDUSPACE_MEDIA_POLICY_BACKEND_CONNECT_TIMEOUT_MS"
                },
                config.backendConnectTimeoutMs);
            config.backendOperationTimeoutMs = readIntEnv(
                {
                    "MEETSPACE_MEDIA_POLICY_BACKEND_OPERATION_TIMEOUT_MS",
                    "EDUSPACE_MEDIA_POLICY_BACKEND_OPERATION_TIMEOUT_MS"
                },
                config.backendOperationTimeoutMs);
            config.backendMaxRetries = readIntEnv(
                { "MEETSPACE_MEDIA_POLICY_BACKEND_MAX_RETRIES", "EDUSPACE_MEDIA_POLICY_BACKEND_MAX_RETRIES" },
                config.backendMaxRetries);
            config.allowTestRtpInjection = readBoolEnv(
                {
                    "MEETSPACE_MEDIA_POLICY_ALLOW_TEST_RTP_INJECTION",
                    "EDUSPACE_MEDIA_POLICY_ALLOW_TEST_RTP_INJECTION"
                },
                config.allowTestRtpInjection);
            config.enforceTrackTypeValidation = readBoolEnv(
                {
                    "MEETSPACE_MEDIA_POLICY_ENFORCE_TRACK_TYPE",
                    "EDUSPACE_MEDIA_POLICY_ENFORCE_TRACK_TYPE"
                },
                config.enforceTrackTypeValidation);
            config.enforceKindValidation = readBoolEnv(
                { "MEETSPACE_MEDIA_POLICY_ENFORCE_KIND", "EDUSPACE_MEDIA_POLICY_ENFORCE_KIND" },
                config.enforceKindValidation);
            return normalizeConfig(std::move(config));
        }

        explicit MediaProductionPolicy(MediaProductionPolicyConfig config = fromEnvironment())
            : config_(normalizeConfig(std::move(config))) {
        }

        const MediaProductionPolicyConfig& config() const noexcept {
            return config_;
        }

        int backendConnectTimeoutMs() const noexcept {
            return config_.backendConnectTimeoutMs;
        }

        int backendOperationTimeoutMs() const noexcept {
            return config_.backendOperationTimeoutMs;
        }

        int backendMaxRetries() const noexcept {
            return config_.backendMaxRetries;
        }

        core::contracts::OperationStatus validateAndConsume(
            service::MediaTransportIntent intent,
            const service::MediaTransportCommand& command,
            std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) {
            const auto validationStatus = validateCommand(intent, command);
            if (!validationStatus.ok) {
                return validationStatus;
            }

            if (command.peerId.empty() || !shouldRateLimit(intent)) {
                return core::contracts::OperationStatus::success();
            }

            return consumeRateBudget(command.peerId, now);
        }

    private:
        static std::string readEnvVar(const char* name) {
            const auto value = std::getenv(name);
            if (value == nullptr || value[0] == '\0') {
                return {};
            }
            return std::string(value);
        }

        static std::string readFirstEnvVar(std::initializer_list<const char*> names) {
            for (const auto* name : names) {
                if (name == nullptr || name[0] == '\0') {
                    continue;
                }
                auto value = readEnvVar(name);
                if (!value.empty()) {
                    return value;
                }
            }
            return {};
        }

        static bool parseBoolean(std::string value, bool& result) {
            if (value.empty()) {
                return false;
            }
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char symbol) {
                return static_cast<char>(std::tolower(symbol));
                });

            if (value == "1" || value == "true" || value == "yes" || value == "on") {
                result = true;
                return true;
            }
            if (value == "0" || value == "false" || value == "no" || value == "off") {
                result = false;
                return true;
            }
            return false;
        }

        static bool readBoolEnv(std::initializer_list<const char*> names, bool fallbackValue) {
            for (const auto* name : names) {
                if (name == nullptr || name[0] == '\0') {
                    continue;
                }
                auto value = readEnvVar(name);
                if (value.empty()) {
                    continue;
                }

                bool parsed = fallbackValue;
                if (parseBoolean(value, parsed)) {
                    return parsed;
                }
            }
            return fallbackValue;
        }

        static int readIntEnv(std::initializer_list<const char*> names, int fallbackValue) {
            const auto value = readFirstEnvVar(names);
            if (value.empty()) {
                return fallbackValue;
            }
            try {
                return std::stoi(value);
            }
            catch (...) {
                return fallbackValue;
            }
        }

        static bool isKnownKind(std::string_view kind) {
            return kind == "audio" || kind == "video" || kind == "data";
        }

        static bool isKnownTrackType(std::string_view trackType) {
            return trackType == "microphone"
                || trackType == "camera"
                || trackType == "screen"
                || trackType == "screen_share"
                || trackType == "screenshare"
                || trackType == "video";
        }

        static bool shouldRateLimit(service::MediaTransportIntent intent) {
            return intent != service::MediaTransportIntent::CloseSession;
        }

        static MediaProductionPolicyConfig normalizeConfig(MediaProductionPolicyConfig config) {
            const auto clamp = [](int value, int min, int max) {
                return std::min(max, std::max(min, value));
                };

            config.maxIdentifierLength = clamp(config.maxIdentifierLength, 16, 2048);
            config.maxSdpBytes = clamp(config.maxSdpBytes, 1024, 1024 * 1024);
            config.maxCandidateBytes = clamp(config.maxCandidateBytes, 256, 512 * 1024);
            config.maxJsonPayloadBytes = clamp(config.maxJsonPayloadBytes, 1024, 2 * 1024 * 1024);
            config.maxActionsPerWindow = clamp(config.maxActionsPerWindow, 1, 10000);
            config.actionWindowSeconds = clamp(config.actionWindowSeconds, 1, 300);
            config.backendConnectTimeoutMs = clamp(config.backendConnectTimeoutMs, 500, 60000);
            config.backendOperationTimeoutMs = clamp(config.backendOperationTimeoutMs, 500, 60000);
            config.backendMaxRetries = clamp(config.backendMaxRetries, 0, 5);
            return config;
        }

        static core::contracts::OperationStatus policyFailure(
            std::string message,
            std::string errorCode) {
            return core::contracts::OperationStatus::failure(
                std::move(message),
                nlohmann::json{
                    { "errorCode", std::move(errorCode) },
                    { "policy", "media_production" }
                });
        }

        core::contracts::OperationStatus validateCommand(
            service::MediaTransportIntent intent,
            const service::MediaTransportCommand& command) const {
            const auto exceedsLimit = [&](std::string_view value, int limit) {
                return !value.empty() && static_cast<int>(value.size()) > limit;
                };

            for (const auto& identifier : {
                std::string_view(command.sessionId),
                std::string_view(command.peerId),
                std::string_view(command.roomId),
                std::string_view(command.transportId),
                std::string_view(command.producerId),
                std::string_view(command.consumerId),
                std::string_view(command.correlationId)
                }) {
                if (exceedsLimit(identifier, config_.maxIdentifierLength)) {
                    return policyFailure(
                        "Identifier length exceeds media production policy limit.",
                        "validation_error");
                }
            }

            if (exceedsLimit(command.sdp, config_.maxSdpBytes)) {
                return policyFailure("SDP payload exceeds media production policy limit.", "validation_error");
            }
            if (exceedsLimit(command.candidate, config_.maxCandidateBytes)) {
                return policyFailure("ICE candidate payload exceeds media production policy limit.", "validation_error");
            }

            for (const auto& jsonPayload : {
                std::string_view(command.dtlsParameters),
                std::string_view(command.rtpParameters),
                std::string_view(command.rtpCapabilities)
                }) {
                if (exceedsLimit(jsonPayload, config_.maxJsonPayloadBytes)) {
                    return policyFailure("JSON payload exceeds media production policy limit.", "validation_error");
                }
            }

            if (config_.enforceKindValidation && !command.kind.empty() && !isKnownKind(command.kind)) {
                return policyFailure("Unsupported media kind by production policy.", "validation_error");
            }
            if (config_.enforceTrackTypeValidation
                && !command.trackType.empty()
                && !isKnownTrackType(command.trackType)) {
                return policyFailure("Unsupported media track type by production policy.", "validation_error");
            }
            if (!config_.allowTestRtpInjection && command.injectTestRtp) {
                return policyFailure("Synthetic RTP injection is disabled in production policy.", "forbidden");
            }

            if (intent == service::MediaTransportIntent::ApplyOffer && command.dtlsParameters.empty()) {
                return policyFailure("dtlsParameters are required by production policy for webrtc_offer.", "validation_error");
            }

            return core::contracts::OperationStatus::success();
        }

        core::contracts::OperationStatus consumeRateBudget(
            std::string_view peerId,
            std::chrono::steady_clock::time_point now) {
            const auto window = std::chrono::seconds(config_.actionWindowSeconds);
            auto& history = peerActionHistory_[std::string(peerId)];
            while (!history.empty() && (now - history.front()) > window) {
                history.pop_front();
            }

            if (static_cast<int>(history.size()) >= config_.maxActionsPerWindow) {
                return policyFailure("Media action rate limit exceeded for peer.", "rate_limited");
            }

            history.push_back(now);
            return core::contracts::OperationStatus::success();
        }

    private:
        MediaProductionPolicyConfig config_;
        std::unordered_map<std::string, std::deque<std::chrono::steady_clock::time_point>> peerActionHistory_;
    };

} // namespace eds::server_new::mediasoup::policy
