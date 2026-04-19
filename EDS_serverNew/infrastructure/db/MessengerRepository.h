#pragma once

#include "Auth/SessionAuthStore.h"
#include "contracts/Primitives.h"
#include "infrastructure/db/PostgresClient.h"

#include <memory>
#include <nlohmann/json.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace eds::server_new::infrastructure::db {

    class MessengerRepository final {
    public:
        using json = nlohmann::json;

        explicit MessengerRepository(std::shared_ptr<PostgresClient> client);

        bool isReady() const noexcept;

        core::contracts::OperationStatus mirrorRealtimeSession(
            const eds::server_new::auth::AuthenticatedSession& session);

        core::contracts::OperationStatus markRealtimeSessionDisconnected(
            std::uintptr_t sessionHandle,
            std::string_view peerId);
        core::contracts::OperationStatus markServerNodeSessionsDisconnected();

        core::contracts::OperationStatus handleConferencePeerDisconnected(
            std::string_view peerId,
            std::uintptr_t sessionHandle);

        core::contracts::OperationStatus handleDirectCallPeerDisconnected(
            std::string_view peerId,
            std::uintptr_t sessionHandle);

        core::contracts::OperationStatus createConference(
            std::string_view ownerUserId,
            std::string_view conferencePublicId,
            std::string_view ownerPeerId,
            std::uintptr_t sessionHandle);

        core::contracts::OperationStatus getConference(
            std::string_view requesterUserId,
            std::string_view conferencePublicId) const;

        core::contracts::OperationStatus closeConference(
            std::string_view requesterUserId,
            std::string_view conferencePublicId);

        core::contracts::OperationStatus joinConference(
            std::string_view requesterUserId,
            std::string_view conferencePublicId,
            std::string_view peerId,
            std::uintptr_t sessionHandle);

        core::contracts::OperationStatus leaveConference(
            std::string_view requesterUserId,
            std::string_view conferencePublicId,
            std::string_view peerId,
            std::uintptr_t sessionHandle);

        core::contracts::OperationStatus listConferenceMembers(
            std::string_view requesterUserId,
            std::string_view conferencePublicId) const;
        core::contracts::OperationStatus resolveConferenceMediaContext(
            std::string_view requesterUserId,
            std::string_view conferencePublicId) const;

        core::contracts::OperationStatus sendConferenceMessage(
            std::string_view senderUserId,
            std::string_view senderPeerId,
            std::string_view conferencePublicId,
            std::string_view targetUserId,
            std::string_view clientRequestId,
            std::string_view text);
        core::contracts::OperationStatus listConferenceMessages(
            std::string_view requesterUserId,
            std::string_view conferencePublicId,
            std::size_t limit,
            std::string_view beforeCreatedAt,
            std::string_view afterCreatedAt) const;
        core::contracts::OperationStatus ackConferenceMessages(
            std::string_view requesterUserId,
            std::string_view conferencePublicId,
            const std::vector<std::string>& messageIds,
            bool markRead);

        core::contracts::OperationStatus sendDirectMessage(
            std::string_view senderUserId,
            std::string_view senderPeerId,
            std::string_view targetUserId,
            std::string_view clientRequestId,
            std::string_view text);
        core::contracts::OperationStatus listDirectThreads(
            std::string_view requesterUserId,
            std::size_t limit) const;
        core::contracts::OperationStatus listDirectMessages(
            std::string_view requesterUserId,
            std::string_view targetUserId,
            std::string_view threadId,
            std::size_t limit,
            std::string_view beforeCreatedAt,
            std::string_view afterCreatedAt) const;
        core::contracts::OperationStatus ackDirectMessages(
            std::string_view requesterUserId,
            std::string_view targetUserId,
            std::string_view threadId,
            const std::vector<std::string>& messageIds,
            bool markRead);
        core::contracts::OperationStatus searchUsersByEmail(
            std::string_view requesterUserId,
            std::string_view query,
            std::size_t limit) const;
        core::contracts::OperationStatus claimPendingOfflineOutbox(std::size_t limit);
        core::contracts::OperationStatus markOfflineOutboxDelivered(std::int64_t outboxId);
        core::contracts::OperationStatus markOfflineOutboxRetry(
            std::int64_t outboxId,
            std::string_view reason,
            std::int32_t maxAttempts = 8);

        core::contracts::OperationStatus createDirectCall(
            std::string_view callerUserId,
            std::string_view callerPeerId,
            std::string_view calleeUserId,
            std::string_view clientRequestId);
        core::contracts::OperationStatus listUserActiveDirectCalls(
            std::string_view userId,
            std::size_t limit) const;
        core::contracts::OperationStatus listUserConferences(
            std::string_view userId,
            std::size_t limit) const;
        core::contracts::OperationStatus resolveDirectCallMediaContext(
            std::string_view actorUserId,
            std::string_view callPublicId) const;

        core::contracts::OperationStatus acceptDirectCall(
            std::string_view calleeUserId,
            std::string_view calleePeerId,
            std::string_view callPublicId);

        core::contracts::OperationStatus declineDirectCall(
            std::string_view calleeUserId,
            std::string_view calleePeerId,
            std::string_view callPublicId);

        core::contracts::OperationStatus hangupDirectCall(
            std::string_view actorUserId,
            std::string_view actorPeerId,
            std::string_view callPublicId);

    private:
        static core::contracts::OperationStatus dbFailure(std::string error);

        core::contracts::OperationStatus querySingleJson(
            const std::string& sql,
            const std::vector<std::string>& params,
            std::string successMessage) const;

        core::contracts::OperationStatus executeOnly(
            const std::string& sql,
            const std::vector<std::string>& params,
            std::string successMessage) const;

        static std::string sha256Hex(std::string_view text);
        static std::string effectiveDeviceKey(const eds::server_new::auth::AuthenticatedSession& session);
        static std::string readServerNode();
        static std::size_t clampBatchSize(std::size_t value, std::size_t minValue, std::size_t maxValue);
        static std::string encodeJsonArray(const std::vector<std::string>& values);

    private:
        std::shared_ptr<PostgresClient> client_;
        std::string serverNode_;
    };

    std::shared_ptr<MessengerRepository> sharedMessengerRepository();
    bool configureSharedMessengerRepository(const std::string& conninfo, std::string& error);

} // namespace eds::server_new::infrastructure::db