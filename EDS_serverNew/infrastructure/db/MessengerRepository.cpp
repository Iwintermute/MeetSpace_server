#include "infrastructure/db/MessengerRepository.h"

#include <openssl/sha.h>

#include <cstdlib>
#include <iomanip>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <utility>

namespace eds::server_new::infrastructure::db {
    namespace {

        std::mutex gRepositoryMutex;
        std::shared_ptr<MessengerRepository> gSharedRepository;

        std::string toStringHandle(std::uintptr_t value) {
            return std::to_string(static_cast<unsigned long long>(value));
        }

    } // namespace

    MessengerRepository::MessengerRepository(std::shared_ptr<PostgresClient> client)
        : client_(std::move(client)),
        serverNode_(readServerNode()) {
    }

    bool MessengerRepository::isReady() const noexcept {
        return client_ != nullptr && client_->isConnected();
    }

    core::contracts::OperationStatus MessengerRepository::dbFailure(std::string error) {
        return core::contracts::OperationStatus::failure(std::move(error));
    }

    core::contracts::OperationStatus MessengerRepository::querySingleJson(
        const std::string& sql,
        const std::vector<std::string>& params,
        std::string successMessage) const {
        if (!client_) {
            return dbFailure("MessengerRepository has no PostgresClient.");
        }

        std::string error;
        auto payload = client_->queryScalarJson(sql, params, error);
        if (!payload.has_value()) {
            if (error.empty()) {
                error = "Database query returned no rows.";
            }
            return dbFailure(std::move(error));
        }

        return core::contracts::OperationStatus::success(std::move(successMessage), std::move(*payload));
    }

    core::contracts::OperationStatus MessengerRepository::executeOnly(
        const std::string& sql,
        const std::vector<std::string>& params,
        std::string successMessage) const {
        if (!client_) {
            return dbFailure("MessengerRepository has no PostgresClient.");
        }

        std::string error;
        if (!client_->execute(sql, params, error)) {
            return dbFailure(std::move(error));
        }

        return core::contracts::OperationStatus::success(std::move(successMessage));
    }

    std::string MessengerRepository::sha256Hex(std::string_view text) {
        unsigned char digest[SHA256_DIGEST_LENGTH]{};
        SHA256(reinterpret_cast<const unsigned char*>(text.data()), text.size(), digest);

        std::ostringstream stream;
        stream << std::hex << std::setfill('0');
        for (const auto byte : digest) {
            stream << std::setw(2) << static_cast<int>(byte);
        }
        return stream.str();
    }

    std::string MessengerRepository::effectiveDeviceKey(
        const eds::server_new::auth::AuthenticatedSession& session) {
        if (!session.deviceId.empty()) {
            return session.deviceId;
        }
        if (!session.peerId.empty()) {
            return "peer_" + session.peerId;
        }
        return "device_unknown";
    }

    std::string MessengerRepository::readServerNode() {
        const char* raw = std::getenv("EDUSPACE_SERVER_NODE");
        if (raw == nullptr || raw[0] == '\0') {
            return "server-default";
        }
        return std::string(raw);
    }

    std::size_t MessengerRepository::clampBatchSize(
        std::size_t value,
        std::size_t minValue,
        std::size_t maxValue) {
        if (value < minValue) {
            return minValue;
        }
        if (value > maxValue) {
            return maxValue;
        }
        return value;
    }

    std::string MessengerRepository::encodeJsonArray(const std::vector<std::string>& values) {
        json payload = json::array();
        for (const auto& value : values) {
            if (!value.empty()) {
                payload.push_back(value);
            }
        }
        return payload.dump();
    }

    core::contracts::OperationStatus MessengerRepository::mirrorRealtimeSession(
        const eds::server_new::auth::AuthenticatedSession& session) {
        if (!isReady()) {
            return dbFailure("Repository is not ready.");
        }
        if (session.userId.empty()) {
            return dbFailure("mirrorRealtimeSession: userId must not be empty.");
        }
        if (session.peerId.empty()) {
            return dbFailure("mirrorRealtimeSession: peerId must not be empty.");
        }

        const auto deviceKey = effectiveDeviceKey(session);
        const auto accessTokenHash =
            session.accessToken.empty() ? std::string{} : sha256Hex(session.accessToken);

        static const std::string sql = R"SQL(
WITH upsert_device AS (
    INSERT INTO app.devices (
        user_id,
        device_key,
        platform,
        device_name,
        metadata
    )
    VALUES (
        $1::uuid,
        $2,
        'client',
        $2,
        jsonb_build_object(
            'peerId', $3::text,
            'sessionHandle', $4::bigint
        )
    )
    ON CONFLICT (user_id, device_key) DO UPDATE
        SET platform = COALESCE(NULLIF(EXCLUDED.platform, ''), app.devices.platform),
            device_name = COALESCE(NULLIF(EXCLUDED.device_name, ''), app.devices.device_name),
            metadata = app.devices.metadata || EXCLUDED.metadata,
            updated_at = timezone('utc', now())
    RETURNING id
),
upsert_session AS (
    INSERT INTO app.user_sessions (
        user_id,
        device_id,
        peer_id,
        server_node,
        status,
        access_token_sha256,
        last_seen_at,
        connected_at,
        metadata
    )
    SELECT
        $1::uuid,
        d.id,
        $3,
        $5,
        'connected',
        NULLIF($6, ''),
        timezone('utc', now()),
        timezone('utc', now()),
        jsonb_build_object(
            'email', $7::text,
            'peerId', $3::text,
            'sessionHandle', $4::bigint,
            'deviceKey', $2::text
        )
    FROM upsert_device d
    ON CONFLICT (peer_id) DO UPDATE
        SET user_id = EXCLUDED.user_id,
            device_id = EXCLUDED.device_id,
            server_node = EXCLUDED.server_node,
            status = 'connected',
            access_token_sha256 = EXCLUDED.access_token_sha256,
            last_seen_at = timezone('utc', now()),
            disconnected_at = NULL,
            metadata = app.user_sessions.metadata || EXCLUDED.metadata
    RETURNING id, connection_id, peer_id, user_id, server_node
)
SELECT json_build_object(
    'sessionId', id::text,
    'connectionId', connection_id::text,
    'peerId', peer_id,
    'userId', user_id::text,
    'serverNode', server_node
)
FROM upsert_session
LIMIT 1
)SQL";

        return querySingleJson(
            sql,
            {
                session.userId,
                deviceKey,
                session.peerId,
                toStringHandle(session.sessionHandle),
                serverNode_,
                accessTokenHash,
                session.email
            },
            "Realtime session mirrored.");
    }

    core::contracts::OperationStatus MessengerRepository::markRealtimeSessionDisconnected(
        std::uintptr_t sessionHandle,
        std::string_view peerId) {
        if (!isReady()) {
            return dbFailure("Repository is not ready.");
        }
        if (peerId.empty()) {
            return dbFailure("markRealtimeSessionDisconnected: peerId must not be empty.");
        }

        static const std::string sql = R"SQL(
WITH updated AS (
    UPDATE app.user_sessions
       SET status = 'disconnected',
           disconnected_at = timezone('utc', now()),
           last_seen_at = timezone('utc', now()),
           metadata = app.user_sessions.metadata || jsonb_build_object('disconnectSessionHandle', $2::bigint)
     WHERE peer_id = $1
       AND status = 'connected'
     RETURNING id, peer_id, user_id
)
SELECT COALESCE(
    (
        SELECT json_build_object(
            'disconnected', true,
            'sessionId', id::text,
            'peerId', peer_id,
            'userId', user_id::text
        )
        FROM updated
        LIMIT 1
    ),
    json_build_object(
        'disconnected', false,
        'peerId', $1
    )
)
)SQL";

        return querySingleJson(
            sql,
            {
                std::string(peerId),
                toStringHandle(sessionHandle)
            },
            "Realtime session disconnected.");
    }

    core::contracts::OperationStatus MessengerRepository::markServerNodeSessionsDisconnected() {
        if (!isReady()) {
            return dbFailure("Repository is not ready.");
        }

        static const std::string sql = R"SQL(
WITH updated AS (
    UPDATE app.user_sessions us
       SET status = 'disconnected',
           disconnected_at = COALESCE(us.disconnected_at, timezone('utc', now())),
           last_seen_at = timezone('utc', now()),
           metadata = us.metadata || jsonb_build_object(
               'cleanupServerNode', $1,
               'cleanupReason', 'startup_stale_session_cleanup',
               'cleanupScope', 'all_connected_sessions',
               'cleanupAt', timezone('utc', now())
           )
     WHERE us.status = 'connected'
     RETURNING us.id
)
SELECT json_build_object(
    'serverNode', $1,
    'disconnectedCount', (SELECT COUNT(*) FROM updated)
)
)SQL";

        return querySingleJson(
            sql,
            { serverNode_ },
            "Server-node stale sessions disconnected.");
    }

    core::contracts::OperationStatus MessengerRepository::createConference(
        std::string_view ownerUserId,
        std::string_view conferencePublicId,
        std::string_view ownerPeerId,
        std::uintptr_t sessionHandle) {
        if (!isReady()) {
            return dbFailure("Repository is not ready.");
        }
        if (ownerUserId.empty() || conferencePublicId.empty() || ownerPeerId.empty()) {
            return dbFailure("createConference: ownerUserId, conferencePublicId and ownerPeerId must not be empty.");
        }

        static const std::string sql = R"SQL(
WITH actor_session AS (
    SELECT us.id
      FROM app.user_sessions us
     WHERE us.peer_id = $3
       AND us.status = 'connected'
     LIMIT 1
),
upsert_conference AS (
    INSERT INTO app.conferences (
        public_id,
        owner_user_id,
        title,
        conference_kind,
        status,
        metadata
    )
    VALUES (
        $2,
        $1::uuid,
        $2,
        'meeting',
        'active',
        jsonb_build_object(
            'ownerPeerId', $3,
            'creatorSessionHandle', $4::bigint
        )
    )
    ON CONFLICT (public_id) DO UPDATE
        SET updated_at = timezone('utc', now())
    RETURNING id, public_id, owner_user_id, status
),
owner_member AS (
    INSERT INTO app.conference_members (
        conference_id,
        user_id,
        role,
        membership_status,
        joined_at,
        metadata
    )
    SELECT
        c.id,
        $1::uuid,
        'owner',
        'joined',
        timezone('utc', now()),
        jsonb_build_object('ownerPeerId', $3)
    FROM upsert_conference c
    ON CONFLICT (conference_id, user_id) DO UPDATE
        SET role = 'owner',
            membership_status = 'joined',
            joined_at = COALESCE(app.conference_members.joined_at, EXCLUDED.joined_at),
            left_at = NULL,
            updated_at = timezone('utc', now()),
            metadata = app.conference_members.metadata || EXCLUDED.metadata
    RETURNING conference_id
),
owner_session AS (
    INSERT INTO app.conference_sessions (
        conference_id,
        session_id,
        user_id,
        joined_at,
        media_state,
        metadata
    )
    SELECT
        c.id,
        s.id,
        $1::uuid,
        timezone('utc', now()),
        'joined',
        jsonb_build_object('peerId', $3)
    FROM upsert_conference c
    CROSS JOIN actor_session s
    ON CONFLICT (conference_id, session_id) DO UPDATE
        SET left_at = NULL,
            media_state = 'joined',
            metadata = app.conference_sessions.metadata || EXCLUDED.metadata
    RETURNING conference_id
),
alloc AS (
    INSERT INTO app.media_room_allocations (
        media_room_id,
        owner_object_type,
        owner_object_id,
        room_id,
        owner_type,
        owner_public_id,
        backend_engine,
        backend_node,
        metadata
    )
    SELECT
        'conf_' || $2,
        'conference',
        c.id,
        'conf_' || $2,
        'conference',
        $2,
        'mediasoup',
        $5,
        jsonb_build_object('conferencePublicId', $2)
    FROM upsert_conference c
    ON CONFLICT (owner_type, owner_public_id) DO UPDATE
        SET media_room_id = EXCLUDED.media_room_id,
            owner_object_type = EXCLUDED.owner_object_type,
            owner_object_id = EXCLUDED.owner_object_id,
            room_id = EXCLUDED.room_id,
            backend_engine = EXCLUDED.backend_engine,
            backend_node = EXCLUDED.backend_node,
            metadata = app.media_room_allocations.metadata || EXCLUDED.metadata
    RETURNING room_id
)
SELECT json_build_object(
    'conferencePublicId', $2,
    'ownerUserId', $1,
    'ownerPeerId', $3,
    'mediaRoomId', (SELECT room_id FROM alloc LIMIT 1),
    'status', 'active',
    'outboundEvents', '[]'::json
)
)SQL";

        return querySingleJson(
            sql,
            {
                std::string(ownerUserId),
                std::string(conferencePublicId),
                std::string(ownerPeerId),
                toStringHandle(sessionHandle),
                serverNode_
            },
            "Conference created.");
    }

    core::contracts::OperationStatus MessengerRepository::getConference(
        std::string_view requesterUserId,
        std::string_view conferencePublicId) const {
        if (!isReady()) {
            return dbFailure("Repository is not ready.");
        }
        if (requesterUserId.empty() || conferencePublicId.empty()) {
            return dbFailure("getConference: requesterUserId and conferencePublicId must not be empty.");
        }

        static const std::string sql = R"SQL(
WITH target AS (
    SELECT
        c.id,
        c.public_id,
        c.owner_user_id,
        c.title,
        c.conference_kind,
        c.status,
        c.created_at,
        c.updated_at,
        c.closed_at
      FROM app.conferences c
      LEFT JOIN app.conference_members m
        ON m.conference_id = c.id
       AND m.user_id = $1::uuid
     WHERE c.public_id = $2
       AND (
            c.owner_user_id = $1::uuid
            OR m.membership_status IN ('invited', 'joined', 'left')
       )
     LIMIT 1
)
SELECT json_build_object(
    'conferenceId', id::text,
    'conferencePublicId', public_id,
    'ownerUserId', owner_user_id::text,
    'title', title,
    'conferenceKind', conference_kind,
    'status', status,
    'createdAt', created_at,
    'updatedAt', updated_at,
    'closedAt', closed_at,
    'mediaRoomId', (
        SELECT a.room_id
          FROM app.media_room_allocations a
         WHERE a.owner_type = 'conference'
           AND a.owner_public_id = public_id
         LIMIT 1
    )
)
FROM target
LIMIT 1
)SQL";

        return querySingleJson(
            sql,
            {
                std::string(requesterUserId),
                std::string(conferencePublicId)
            },
            "Conference fetched.");
    }

    core::contracts::OperationStatus MessengerRepository::closeConference(
        std::string_view requesterUserId,
        std::string_view conferencePublicId) {
        if (!isReady()) {
            return dbFailure("Repository is not ready.");
        }
        if (requesterUserId.empty() || conferencePublicId.empty()) {
            return dbFailure("closeConference: requesterUserId and conferencePublicId must not be empty.");
        }

        static const std::string sql = R"SQL(
WITH target AS (
    SELECT c.id, c.public_id
      FROM app.conferences c
     WHERE c.public_id = $2
       AND c.owner_user_id = $1::uuid
     LIMIT 1
),
active_sessions AS (
    SELECT us.id AS session_id, us.user_id, us.peer_id, us.server_node
      FROM target t
      JOIN app.conference_sessions cs
        ON cs.conference_id = t.id
       AND cs.left_at IS NULL
      JOIN app.user_sessions us
        ON us.id = cs.session_id
       AND us.status = 'connected'
),
outbox AS (
    INSERT INTO app.realtime_outbox (
        server_node,
        target_user_id,
        target_session_id,
        target_peer_id,
        recipient_user_id,
        recipient_session_id,
        aggregate_type,
        aggregate_id,
        event_type,
        payload,
        status
    )
    SELECT
        s.server_node,
        s.user_id,
        s.session_id,
        s.peer_id,
        s.user_id,
        s.session_id,
        'conference',
        t.public_id,
        'conference_closed',
        jsonb_build_object(
            'type', 'conference_closed',
            'object', 'conference',
            'conferencePublicId', t.public_id
        ),
        'pending'
    FROM target t
    JOIN active_sessions s ON TRUE
    RETURNING id
),
deleted_alloc AS (
    DELETE FROM app.media_room_allocations a
     USING target t
     WHERE a.owner_type = 'conference'
       AND a.owner_public_id = t.public_id
     RETURNING a.room_id
),
deleted_conf AS (
    DELETE FROM app.conferences c
     USING target t
     WHERE c.id = t.id
     RETURNING c.public_id
)
SELECT COALESCE(
    (
        SELECT json_build_object(
            'conferencePublicId', t.public_id,
            'mediaRoomId', (SELECT room_id FROM deleted_alloc LIMIT 1),
            'activePeerIds', COALESCE((SELECT json_agg(s.peer_id) FROM active_sessions s), '[]'::json),
            'conferenceDeleted', true,
            'outboundEvents', COALESCE((
                SELECT json_agg(
                    json_build_object(
                        'type', 'conference_closed',
                        'object', 'conference',
                        'conferencePublicId', t.public_id,
                        'deliverTo', s.peer_id
                    )
                )
                FROM active_sessions s
            ), '[]'::json)
        )
        FROM target t
        LIMIT 1
    ),
    json_build_object(
        'conferencePublicId', $2,
        'conferenceDeleted', false,
        'activePeerIds', '[]'::json,
        'outboundEvents', '[]'::json
    )
)
)SQL";

        return querySingleJson(
            sql,
            {
                std::string(requesterUserId),
                std::string(conferencePublicId)
            },
            "Conference closed.");
    }

    core::contracts::OperationStatus MessengerRepository::joinConference(
        std::string_view requesterUserId,
        std::string_view conferencePublicId,
        std::string_view peerId,
        std::uintptr_t sessionHandle) {
        if (!isReady()) {
            return dbFailure("Repository is not ready.");
        }
        if (requesterUserId.empty() || conferencePublicId.empty() || peerId.empty()) {
            return dbFailure("joinConference: requesterUserId, conferencePublicId and peerId must not be empty.");
        }

        static const std::string sql = R"SQL(
WITH target AS (
    SELECT c.id, c.public_id
      FROM app.conferences c
     WHERE c.public_id = $2
       AND c.status = 'active'
     LIMIT 1
),
actor_session AS (
    SELECT us.id
      FROM app.user_sessions us
     WHERE us.peer_id = $3
       AND us.status = 'connected'
     LIMIT 1
),
upsert_member AS (
    INSERT INTO app.conference_members (
        conference_id,
        user_id,
        role,
        membership_status,
        joined_at,
        metadata
    )
    SELECT
        t.id,
        $1::uuid,
        'member',
        'joined',
        timezone('utc', now()),
        jsonb_build_object('joinedPeerId', $3, 'sessionHandle', $4::bigint)
    FROM target t
    ON CONFLICT (conference_id, user_id) DO UPDATE
        SET membership_status = 'joined',
            left_at = NULL,
            joined_at = COALESCE(app.conference_members.joined_at, EXCLUDED.joined_at),
            updated_at = timezone('utc', now()),
            metadata = app.conference_members.metadata || EXCLUDED.metadata
    RETURNING conference_id
),
upsert_session AS (
    INSERT INTO app.conference_sessions (
        conference_id,
        session_id,
        user_id,
        joined_at,
        media_state,
        metadata
    )
    SELECT
        t.id,
        s.id,
        $1::uuid,
        timezone('utc', now()),
        'joined',
        jsonb_build_object('peerId', $3)
    FROM target t
    CROSS JOIN actor_session s
    ON CONFLICT (conference_id, session_id) DO UPDATE
        SET left_at = NULL,
            media_state = 'joined',
            metadata = app.conference_sessions.metadata || EXCLUDED.metadata
    RETURNING conference_id
),
all_active_peers AS (
    SELECT DISTINCT us.peer_id
      FROM target t
      JOIN app.conference_sessions cs
        ON cs.conference_id = t.id
       AND cs.left_at IS NULL
      JOIN app.user_sessions us
        ON us.id = cs.session_id
       AND us.status = 'connected'
),
notify_peers AS (
    SELECT peer_id
      FROM all_active_peers
     WHERE peer_id <> $3
),
outbox AS (
    INSERT INTO app.realtime_outbox (
        server_node,
        target_user_id,
        target_session_id,
        target_peer_id,
        recipient_user_id,
        recipient_session_id,
        aggregate_type,
        aggregate_id,
        event_type,
        payload,
        status
    )
    SELECT
        us.server_node,
        us.user_id,
        us.id,
        us.peer_id,
        us.user_id,
        us.id,
        'conference',
        $2,
        'conference_member_joined',
        jsonb_build_object(
            'type', 'conference_member_joined',
            'object', 'conference',
            'conferencePublicId', $2,
            'userId', $1::uuid::text,
            'peerId', $3
        ),
        'pending'
    FROM app.user_sessions us
    WHERE us.peer_id IN (SELECT peer_id FROM notify_peers)
    RETURNING id
)
SELECT json_build_object(
    'conferencePublicId', $2,
    'mediaRoomId', (
        SELECT a.room_id
          FROM app.media_room_allocations a
         WHERE a.owner_type = 'conference'
           AND a.owner_public_id = $2
         LIMIT 1
    ),
    'activePeerIds', COALESCE((SELECT json_agg(peer_id) FROM all_active_peers), '[]'::json),
    'conferenceDeleted', false,
    'outboundEvents', COALESCE((
        SELECT json_agg(
            json_build_object(
                'type', 'conference_member_joined',
                'object', 'conference',
                'conferencePublicId', $2,
                'userId', $1::uuid::text,
                'peerId', $3,
                'deliverTo', peer_id
            )
        )
        FROM notify_peers
    ), '[]'::json)
)
)SQL";

        return querySingleJson(
            sql,
            {
                std::string(requesterUserId),
                std::string(conferencePublicId),
                std::string(peerId),
                toStringHandle(sessionHandle)
            },
            "Conference joined.");
    }

    core::contracts::OperationStatus MessengerRepository::leaveConference(
        std::string_view requesterUserId,
        std::string_view conferencePublicId,
        std::string_view peerId,
        std::uintptr_t sessionHandle) {
        if (!isReady()) {
            return dbFailure("Repository is not ready.");
        }
        if (requesterUserId.empty() || conferencePublicId.empty() || peerId.empty()) {
            return dbFailure("leaveConference: requesterUserId, conferencePublicId and peerId must not be empty.");
        }

        static const std::string sql = R"SQL(
WITH target AS (
    SELECT c.id, c.public_id
      FROM app.conferences c
     WHERE c.public_id = $2
     LIMIT 1
),
actor_session AS (
    SELECT us.id AS session_id
      FROM app.user_sessions us
     WHERE us.peer_id = $3
       AND us.status = 'connected'
     LIMIT 1
),
closed_session AS (
    UPDATE app.conference_sessions cs
       SET left_at = timezone('utc', now()),
           media_state = 'closed',
           metadata = cs.metadata || jsonb_build_object('leavePeerId', $3, 'sessionHandle', $4::bigint)
      FROM target t
      CROSS JOIN actor_session a
     WHERE cs.conference_id = t.id
       AND cs.session_id = a.session_id
       AND cs.left_at IS NULL
     RETURNING cs.conference_id, cs.user_id
),
remaining_actor_sessions AS (
    SELECT COUNT(*)::bigint AS cnt
      FROM app.conference_sessions cs
      JOIN target t
        ON t.id = cs.conference_id
     WHERE cs.user_id = $1::uuid
       AND cs.left_at IS NULL
),
updated_member AS (
    UPDATE app.conference_members m
       SET membership_status = CASE
               WHEN (SELECT cnt FROM remaining_actor_sessions) = 0 THEN 'left'
               ELSE m.membership_status
           END,
           left_at = CASE
               WHEN (SELECT cnt FROM remaining_actor_sessions) = 0 THEN timezone('utc', now())
               ELSE m.left_at
           END,
           updated_at = timezone('utc', now()),
           metadata = m.metadata || jsonb_build_object('lastLeavePeerId', $3, 'sessionHandle', $4::bigint)
      FROM target t
     WHERE m.conference_id = t.id
       AND m.user_id = $1::uuid
     RETURNING m.conference_id
),
remaining_joined_users AS (
    SELECT COUNT(*)::bigint AS cnt
      FROM app.conference_members m
      JOIN target t
        ON t.id = m.conference_id
     WHERE m.membership_status = 'joined'
),
remaining_peers AS (
    SELECT DISTINCT us.peer_id
      FROM target t
      JOIN app.conference_sessions cs
        ON cs.conference_id = t.id
       AND cs.left_at IS NULL
      JOIN app.user_sessions us
        ON us.id = cs.session_id
       AND us.status = 'connected'
),
outbox AS (
    INSERT INTO app.realtime_outbox (
        server_node,
        target_user_id,
        target_session_id,
        target_peer_id,
        recipient_user_id,
        recipient_session_id,
        aggregate_type,
        aggregate_id,
        event_type,
        payload,
        status
    )
    SELECT
        us.server_node,
        us.user_id,
        us.id,
        us.peer_id,
        us.user_id,
        us.id,
        'conference',
        $2,
        'conference_member_left',
        jsonb_build_object(
            'type', 'conference_member_left',
            'object', 'conference',
            'conferencePublicId', $2,
            'userId', $1::uuid::text,
            'peerId', $3
        ),
        'pending'
    FROM app.user_sessions us
    WHERE us.peer_id IN (SELECT peer_id FROM remaining_peers)
      AND (SELECT cnt FROM remaining_joined_users) > 0
    RETURNING id
),
deleted_alloc AS (
    DELETE FROM app.media_room_allocations a
     WHERE a.owner_type = 'conference'
       AND a.owner_public_id = $2
       AND (SELECT cnt FROM remaining_joined_users) = 0
     RETURNING a.room_id
),
deleted_conf AS (
    DELETE FROM app.conferences c
     USING target t
     WHERE c.id = t.id
       AND (SELECT cnt FROM remaining_joined_users) = 0
     RETURNING c.public_id
)
SELECT json_build_object(
    'conferencePublicId', $2,
    'mediaRoomId', (SELECT room_id FROM deleted_alloc LIMIT 1),
    'conferenceDeleted', EXISTS(SELECT 1 FROM deleted_conf),
    'activePeerIds', COALESCE((SELECT json_agg(peer_id) FROM remaining_peers), '[]'::json),
    'outboundEvents', CASE
        WHEN EXISTS(SELECT 1 FROM deleted_conf) THEN '[]'::json
        ELSE COALESCE((
            SELECT json_agg(
                json_build_object(
                    'type', 'conference_member_left',
                    'object', 'conference',
                    'conferencePublicId', $2,
                    'userId', $1::uuid::text,
                    'peerId', $3,
                    'deliverTo', peer_id
                )
            )
            FROM remaining_peers
        ), '[]'::json)
    END
)
)SQL";

        return querySingleJson(
            sql,
            {
                std::string(requesterUserId),
                std::string(conferencePublicId),
                std::string(peerId),
                toStringHandle(sessionHandle)
            },
            "Conference left.");
    }

    core::contracts::OperationStatus MessengerRepository::listConferenceMembers(
        std::string_view requesterUserId,
        std::string_view conferencePublicId) const {
        if (!isReady()) {
            return dbFailure("Repository is not ready.");
        }
        if (requesterUserId.empty() || conferencePublicId.empty()) {
            return dbFailure("listConferenceMembers: requesterUserId and conferencePublicId must not be empty.");
        }

        static const std::string sql = R"SQL(
WITH target AS (
    SELECT c.id
      FROM app.conferences c
      LEFT JOIN app.conference_members self_m
        ON self_m.conference_id = c.id
       AND self_m.user_id = $1::uuid
     WHERE c.public_id = $2
       AND (
            c.owner_user_id = $1::uuid
            OR self_m.membership_status IN ('invited', 'joined', 'left')
       )
     LIMIT 1
)
SELECT json_build_object(
    'conferencePublicId', $2,
    'members', COALESCE((
        SELECT json_agg(
            json_build_object(
                'userId', m.user_id::text,
                'role', m.role,
                'membershipStatus', m.membership_status,
                'joinedAt', m.joined_at,
                'leftAt', m.left_at
            )
            ORDER BY m.created_at ASC
        )
        FROM app.conference_members m
        JOIN target t ON t.id = m.conference_id
    ), '[]'::json),
    'activePeerIds', COALESCE((
        SELECT json_agg(DISTINCT us.peer_id)
        FROM app.conference_sessions cs
        JOIN app.user_sessions us
          ON us.id = cs.session_id
         AND us.status = 'connected'
        JOIN target t
          ON t.id = cs.conference_id
        WHERE cs.left_at IS NULL
    ), '[]'::json)
)
FROM target
LIMIT 1
)SQL";

        return querySingleJson(
            sql,
            {
                std::string(requesterUserId),
                std::string(conferencePublicId)
            },
            "Conference members listed.");
    }

    core::contracts::OperationStatus MessengerRepository::resolveConferenceMediaContext(
        std::string_view requesterUserId,
        std::string_view conferencePublicId) const {
        if (!isReady()) {
            return dbFailure("Repository is not ready.");
        }
        if (requesterUserId.empty() || conferencePublicId.empty()) {
            return dbFailure("resolveConferenceMediaContext: requesterUserId and conferencePublicId must not be empty.");
        }

        static const std::string sql = R"SQL(
WITH target AS (
    SELECT c.id, c.public_id, c.status
      FROM app.conferences c
      LEFT JOIN app.conference_members self_m
        ON self_m.conference_id = c.id
       AND self_m.user_id = $1::uuid
     WHERE c.public_id = $2
       AND (
            c.owner_user_id = $1::uuid
            OR self_m.membership_status IN ('invited', 'joined', 'left')
       )
     LIMIT 1
),
active_peers AS (
    SELECT DISTINCT us.peer_id
      FROM target t
      JOIN app.conference_sessions cs
        ON cs.conference_id = t.id
       AND cs.left_at IS NULL
      JOIN app.user_sessions us
        ON us.id = cs.session_id
       AND us.status = 'connected'
)
SELECT json_build_object(
    'conferencePublicId', t.public_id,
    'roomId', (
        SELECT a.room_id
          FROM app.media_room_allocations a
         WHERE a.owner_type = 'conference'
           AND a.owner_public_id = t.public_id
         LIMIT 1
    ),
    'status', t.status,
    'activePeerIds', COALESCE((SELECT json_agg(peer_id) FROM active_peers), '[]'::json)
)
FROM target t
LIMIT 1
)SQL";

        return querySingleJson(
            sql,
            {
                std::string(requesterUserId),
                std::string(conferencePublicId)
            },
            "Conference media context resolved.");
    }

    core::contracts::OperationStatus MessengerRepository::sendConferenceMessage(
        std::string_view senderUserId,
        std::string_view senderPeerId,
        std::string_view conferencePublicId,
        std::string_view targetUserId,
        std::string_view clientRequestId,
        std::string_view text) {
        if (!isReady()) {
            return dbFailure("Repository is not ready.");
        }
        if (senderUserId.empty() || senderPeerId.empty() || conferencePublicId.empty()) {
            return dbFailure("sendConferenceMessage: senderUserId, senderPeerId and conferencePublicId must not be empty.");
        }
        if (text.empty()) {
            return dbFailure("sendConferenceMessage: text must not be empty.");
        }
        if (text.size() > 4000) {
            return dbFailure("sendConferenceMessage: text is too long.");
        }

        static const std::string sql = R"SQL(
WITH target AS (
    SELECT c.id, c.public_id
      FROM app.conferences c
      JOIN app.conference_members m
        ON m.conference_id = c.id
       AND m.user_id = $1::uuid
       AND m.membership_status = 'joined'
     WHERE c.public_id = $3
     LIMIT 1
),
sender_session AS (
    SELECT us.id
      FROM app.user_sessions us
     WHERE us.peer_id = $2
       AND us.status = 'connected'
     LIMIT 1
),
upsert_message AS (
    INSERT INTO app.conference_messages (
        conference_id,
        sender_user_id,
        sender_session_id,
        target_user_id,
        body,
        body_type,
        client_request_id,
        metadata
    )
    SELECT
        t.id,
        $1::uuid,
        s.id,
        NULLIF($4, '')::uuid,
        $6,
        'text',
        NULLIF($5, ''),
        jsonb_build_object('senderPeerId', $2)
    FROM target t
    CROSS JOIN sender_session s
    ON CONFLICT (conference_id, sender_user_id, client_request_id) WHERE client_request_id IS NOT NULL
    DO UPDATE SET body = app.conference_messages.body
    RETURNING id, conference_id, sender_user_id, target_user_id, body, created_at, client_request_id
),
recipients AS (
    SELECT m.user_id
      FROM app.conference_members m
      JOIN target t
        ON t.id = m.conference_id
     WHERE m.membership_status = 'joined'
       AND m.user_id <> $1::uuid
       AND (
            NULLIF($4, '') IS NULL
            OR m.user_id = NULLIF($4, '')::uuid
       )
),
receipt_ins AS (
    INSERT INTO app.conference_message_receipts (
        message_id,
        recipient_user_id,
        delivered_at
    )
    SELECT
        m.id,
        r.user_id,
        NULL
    FROM upsert_message m
    CROSS JOIN recipients r
    ON CONFLICT (message_id, recipient_user_id) DO NOTHING
    RETURNING message_id
),
active_sessions AS (
    SELECT s.id AS session_id, s.peer_id, s.user_id, s.server_node
      FROM app.user_sessions s
      JOIN recipients r
        ON r.user_id = s.user_id
     WHERE s.status = 'connected'
),
offline_recipients AS (
    SELECT r.user_id
      FROM recipients r
     WHERE NOT EXISTS (
        SELECT 1
          FROM active_sessions s
         WHERE s.user_id = r.user_id
     )
),
outbox AS (
    INSERT INTO app.realtime_outbox (
        server_node,
        target_user_id,
        target_session_id,
        target_peer_id,
        recipient_user_id,
        recipient_session_id,
        aggregate_type,
        aggregate_id,
        event_type,
        payload,
        status
    )
    SELECT
        a.server_node,
        a.user_id,
        a.session_id,
        a.peer_id,
        a.user_id,
        a.session_id,
        'conference',
        $3,
        'chat_message',
        jsonb_build_object(
            'type', 'chat_message',
            'object', 'chat',
            'conferenceId', $3,
            'messageId', m.id::text,
            'clientRequestId', m.client_request_id,
            'senderUserId', m.sender_user_id::text,
            'senderPeerId', $2,
            'targetUserId', NULLIF($4, ''),
            'text', m.body,
            'createdAt', m.created_at
        ),
        'pending'
    FROM upsert_message m
    CROSS JOIN active_sessions a
    RETURNING id
),
offline_outbox AS (
    INSERT INTO app.realtime_outbox (
        server_node,
        target_user_id,
        target_session_id,
        target_peer_id,
        recipient_user_id,
        recipient_session_id,
        aggregate_type,
        aggregate_id,
        event_type,
        payload,
        status
    )
    SELECT
        $7,
        r.user_id,
        NULL,
        NULL,
        r.user_id,
        NULL,
        'conference',
        $3,
        'chat_message',
        jsonb_build_object(
            'type', 'chat_message',
            'object', 'chat',
            'conferenceId', $3,
            'messageId', m.id::text,
            'clientRequestId', m.client_request_id,
            'senderUserId', m.sender_user_id::text,
            'senderPeerId', $2,
            'targetUserId', NULLIF($4, ''),
            'text', m.body,
            'createdAt', m.created_at
        ),
        'pending'
    FROM upsert_message m
    CROSS JOIN offline_recipients r
    RETURNING id
)
SELECT json_build_object(
    'conferencePublicId', $3,
    'messageId', m.id::text,
    'clientRequestId', m.client_request_id,
    'senderUserId', m.sender_user_id::text,
    'senderPeerId', $2,
    'targetUserId', NULLIF($4, ''),
    'text', m.body,
    'createdAt', m.created_at,
    'outboundEvents', COALESCE((
        SELECT json_agg(
            json_build_object(
                'type', 'chat_message',
                'object', 'chat',
                'conferenceId', $3,
                'messageId', m.id::text,
                'clientRequestId', m.client_request_id,
                'senderUserId', m.sender_user_id::text,
                'senderPeerId', $2,
                'targetUserId', NULLIF($4, ''),
                'text', m.body,
                'createdAt', m.created_at,
                'deliverTo', a.peer_id
            )
        )
        FROM active_sessions a
    ), '[]'::json)
)
FROM upsert_message m
LIMIT 1
)SQL";

        return querySingleJson(
            sql,
            {
                std::string(senderUserId),
                std::string(senderPeerId),
                std::string(conferencePublicId),
                std::string(targetUserId),
                std::string(clientRequestId),
                std::string(text),
                serverNode_
            },
            "Conference message sent.");
    }

    core::contracts::OperationStatus MessengerRepository::listConferenceMessages(
        std::string_view requesterUserId,
        std::string_view conferencePublicId,
        std::size_t limit,
        std::string_view beforeCreatedAt,
        std::string_view afterCreatedAt) const {
        if (!isReady()) {
            return dbFailure("Repository is not ready.");
        }
        if (requesterUserId.empty() || conferencePublicId.empty()) {
            return dbFailure("listConferenceMessages: requesterUserId and conferencePublicId must not be empty.");
        }

        const auto cappedLimit = clampBatchSize(limit, 1, 200);
        static const std::string sql = R"SQL(
WITH target AS (
    SELECT c.id, c.public_id
      FROM app.conferences c
      LEFT JOIN app.conference_members m
        ON m.conference_id = c.id
       AND m.user_id = $1::uuid
     WHERE c.public_id = $2
       AND (
            c.owner_user_id = $1::uuid
            OR m.membership_status IN ('invited', 'joined', 'left')
       )
     LIMIT 1
),
rows AS (
    SELECT
        m.id,
        m.sender_user_id,
        m.target_user_id,
        m.body,
        m.body_type,
        m.client_request_id,
        m.created_at,
        m.edited_at,
        m.deleted_at,
        r.delivered_at AS recipient_delivered_at,
        r.read_at AS recipient_read_at
      FROM app.conference_messages m
      JOIN target t
        ON t.id = m.conference_id
      LEFT JOIN app.conference_message_receipts r
        ON r.message_id = m.id
       AND r.recipient_user_id = $1::uuid
     WHERE m.deleted_at IS NULL
       AND (
            m.target_user_id IS NULL
            OR m.target_user_id = $1::uuid
            OR m.sender_user_id = $1::uuid
       )
       AND (
            NULLIF($4, '') IS NULL
            OR m.created_at < NULLIF($4, '')::timestamptz
       )
       AND (
            NULLIF($5, '') IS NULL
            OR m.created_at > NULLIF($5, '')::timestamptz
       )
     ORDER BY m.created_at DESC, m.id DESC
     LIMIT LEAST(GREATEST($3::int, 1), 200) + 1
),
paged AS (
    SELECT *
      FROM rows
     ORDER BY created_at DESC, id DESC
     LIMIT LEAST(GREATEST($3::int, 1), 200)
),
has_more AS (
    SELECT COUNT(*) > LEAST(GREATEST($3::int, 1), 200) AS value
      FROM rows
)
SELECT json_build_object(
    'conferencePublicId', t.public_id,
    'messages', COALESCE((
        SELECT json_agg(
            json_build_object(
                'messageId', p.id::text,
                'senderUserId', p.sender_user_id::text,
                'targetUserId', p.target_user_id::text,
                'text', p.body,
                'bodyType', p.body_type,
                'clientRequestId', p.client_request_id,
                'createdAt', p.created_at,
                'editedAt', p.edited_at,
                'recipientDeliveredAt', p.recipient_delivered_at,
                'recipientReadAt', p.recipient_read_at
            )
            ORDER BY p.created_at DESC, p.id DESC
        )
          FROM paged p
    ), '[]'::json),
    'hasMore', COALESCE((SELECT value FROM has_more), false),
    'nextBeforeCreatedAt', (
        SELECT p.created_at
          FROM paged p
         ORDER BY p.created_at ASC, p.id ASC
         LIMIT 1
    )
)
FROM target t
LIMIT 1
)SQL";

        return querySingleJson(
            sql,
            {
                std::string(requesterUserId),
                std::string(conferencePublicId),
                std::to_string(cappedLimit),
                std::string(beforeCreatedAt),
                std::string(afterCreatedAt)
            },
            "Conference messages synced.");
    }

    core::contracts::OperationStatus MessengerRepository::ackConferenceMessages(
        std::string_view requesterUserId,
        std::string_view conferencePublicId,
        const std::vector<std::string>& messageIds,
        bool markRead) {
        if (!isReady()) {
            return dbFailure("Repository is not ready.");
        }
        if (requesterUserId.empty() || conferencePublicId.empty()) {
            return dbFailure("ackConferenceMessages: requesterUserId and conferencePublicId must not be empty.");
        }

        const auto messageIdsJson = encodeJsonArray(messageIds);
        static const std::string sql = R"SQL(
WITH target AS (
    SELECT c.id, c.public_id
      FROM app.conferences c
      LEFT JOIN app.conference_members m
        ON m.conference_id = c.id
       AND m.user_id = $1::uuid
     WHERE c.public_id = $2
       AND (
            c.owner_user_id = $1::uuid
            OR m.membership_status IN ('invited', 'joined', 'left')
       )
     LIMIT 1
),
ids AS (
    SELECT DISTINCT value::uuid AS message_id
      FROM jsonb_array_elements_text(COALESCE(NULLIF($3, '')::jsonb, '[]'::jsonb))
     WHERE value ~* '^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$'
),
upsert_receipts AS (
    INSERT INTO app.conference_message_receipts (
        message_id,
        recipient_user_id,
        delivered_at,
        read_at
    )
    SELECT
        m.id,
        $1::uuid,
        timezone('utc', now()),
        CASE
            WHEN $4::boolean THEN timezone('utc', now())
            ELSE NULL
        END
      FROM ids i
      JOIN target t ON TRUE
      JOIN app.conference_messages m
        ON m.id = i.message_id
       AND m.conference_id = t.id
     WHERE m.sender_user_id <> $1::uuid
       AND (
            m.target_user_id IS NULL
            OR m.target_user_id = $1::uuid
            OR m.sender_user_id = $1::uuid
       )
    ON CONFLICT (message_id, recipient_user_id) DO UPDATE
        SET delivered_at = COALESCE(
                app.conference_message_receipts.delivered_at,
                EXCLUDED.delivered_at
            ),
            read_at = CASE
                WHEN $4::boolean THEN COALESCE(
                    app.conference_message_receipts.read_at,
                    EXCLUDED.read_at
                )
                ELSE app.conference_message_receipts.read_at
            END
    RETURNING message_id
),
mark_member AS (
    UPDATE app.conference_members cm
       SET last_read_message_id = (
               SELECT m.id
                 FROM app.conference_messages m
                WHERE m.conference_id = cm.conference_id
                  AND m.id IN (SELECT message_id FROM upsert_receipts)
                ORDER BY m.created_at DESC, m.id DESC
                LIMIT 1
           ),
           updated_at = timezone('utc', now())
      FROM target t
     WHERE cm.conference_id = t.id
       AND cm.user_id = $1::uuid
       AND $4::boolean
       AND EXISTS (SELECT 1 FROM upsert_receipts)
    RETURNING cm.last_read_message_id
)
SELECT json_build_object(
    'conferencePublicId', t.public_id,
    'ackedCount', (SELECT COUNT(*) FROM upsert_receipts),
    'ackedMessageIds', COALESCE((
        SELECT json_agg(message_id::text)
          FROM upsert_receipts
    ), '[]'::json),
    'markRead', $4::boolean
)
FROM target t
LIMIT 1
)SQL";

        return querySingleJson(
            sql,
            {
                std::string(requesterUserId),
                std::string(conferencePublicId),
                messageIdsJson,
                markRead ? "true" : "false"
            },
            "Conference messages acknowledged.");
    }

    core::contracts::OperationStatus MessengerRepository::sendDirectMessage(
        std::string_view senderUserId,
        std::string_view senderPeerId,
        std::string_view targetUserId,
        std::string_view clientRequestId,
        std::string_view text) {
        if (!isReady()) {
            return dbFailure("Repository is not ready.");
        }
        if (senderUserId.empty() || senderPeerId.empty() || targetUserId.empty()) {
            return dbFailure("sendDirectMessage: senderUserId, senderPeerId and targetUserId must not be empty.");
        }
        if (senderUserId == targetUserId) {
            return dbFailure("sendDirectMessage: self direct message is not allowed.");
        }
        if (text.empty()) {
            return dbFailure("sendDirectMessage: text must not be empty.");
        }
        if (text.size() > 4000) {
            return dbFailure("sendDirectMessage: text is too long.");
        }

        static const std::string sql = R"SQL(
WITH thread_cte AS (
    SELECT app.ensure_direct_thread($1::uuid, $3::uuid) AS thread_id
),
sender_session AS (
    SELECT us.id
      FROM app.user_sessions us
     WHERE us.peer_id = $2
       AND us.status = 'connected'
     LIMIT 1
),
upsert_message AS (
    INSERT INTO app.direct_messages (
        thread_id,
        sender_user_id,
        sender_session_id,
        body,
        body_type,
        client_request_id,
        metadata
    )
    SELECT
        t.thread_id,
        $1::uuid,
        s.id,
        $5,
        'text',
        NULLIF($4, ''),
        jsonb_build_object('senderPeerId', $2)
    FROM thread_cte t
    CROSS JOIN sender_session s
    ON CONFLICT (sender_user_id, client_request_id) WHERE client_request_id IS NOT NULL
    DO UPDATE SET body = app.direct_messages.body
    RETURNING id, thread_id, sender_user_id, body, created_at, client_request_id
),
recipients AS (
    SELECT $3::uuid AS user_id
),
receipt_ins AS (
    INSERT INTO app.direct_message_receipts (
        message_id,
        recipient_user_id,
        delivered_at
    )
    SELECT
        m.id,
        r.user_id,
        NULL
    FROM upsert_message m
    CROSS JOIN recipients r
    ON CONFLICT (message_id, recipient_user_id) DO NOTHING
    RETURNING message_id
),
active_sessions AS (
    SELECT us.id AS session_id, us.peer_id, us.user_id, us.server_node
      FROM app.user_sessions us
      JOIN recipients r
        ON r.user_id = us.user_id
     WHERE us.status = 'connected'
),
offline_recipients AS (
    SELECT r.user_id
      FROM recipients r
     WHERE NOT EXISTS (
        SELECT 1
          FROM active_sessions s
         WHERE s.user_id = r.user_id
     )
),
touch_thread AS (
    UPDATE app.direct_threads
       SET last_message_at = timezone('utc', now()),
           updated_at = timezone('utc', now())
     WHERE id = (SELECT thread_id FROM upsert_message LIMIT 1)
     RETURNING id
),
outbox AS (
    INSERT INTO app.realtime_outbox (
        server_node,
        target_user_id,
        target_session_id,
        target_peer_id,
        recipient_user_id,
        recipient_session_id,
        aggregate_type,
        aggregate_id,
        event_type,
        payload,
        status
    )
    SELECT
        a.server_node,
        a.user_id,
        a.session_id,
        a.peer_id,
        a.user_id,
        a.session_id,
        'direct_thread',
        m.thread_id::text,
        'direct_chat_message',
        jsonb_build_object(
            'type', 'direct_chat_message',
            'object', 'direct_chat',
            'threadId', m.thread_id::text,
            'messageId', m.id::text,
            'clientRequestId', m.client_request_id,
            'senderUserId', m.sender_user_id::text,
            'senderPeerId', $2,
            'targetUserId', $3,
            'text', m.body,
            'createdAt', m.created_at
        ),
        'pending'
    FROM upsert_message m
    CROSS JOIN active_sessions a
    RETURNING id
),
offline_outbox AS (
    INSERT INTO app.realtime_outbox (
        server_node,
        target_user_id,
        target_session_id,
        target_peer_id,
        recipient_user_id,
        recipient_session_id,
        aggregate_type,
        aggregate_id,
        event_type,
        payload,
        status
    )
    SELECT
        $6,
        r.user_id,
        NULL,
        NULL,
        r.user_id,
        NULL,
        'direct_thread',
        m.thread_id::text,
        'direct_chat_message',
        jsonb_build_object(
            'type', 'direct_chat_message',
            'object', 'direct_chat',
            'threadId', m.thread_id::text,
            'messageId', m.id::text,
            'clientRequestId', m.client_request_id,
            'senderUserId', m.sender_user_id::text,
            'senderPeerId', $2,
            'targetUserId', $3,
            'text', m.body,
            'createdAt', m.created_at
        ),
        'pending'
    FROM upsert_message m
    CROSS JOIN offline_recipients r
    RETURNING id
)
SELECT json_build_object(
    'threadId', m.thread_id::text,
    'messageId', m.id::text,
    'clientRequestId', m.client_request_id,
    'senderUserId', m.sender_user_id::text,
    'senderPeerId', $2,
    'targetUserId', $3,
    'text', m.body,
    'createdAt', m.created_at,
    'outboundEvents', COALESCE((
        SELECT json_agg(
            json_build_object(
                'type', 'direct_chat_message',
                'object', 'direct_chat',
                'threadId', m.thread_id::text,
                'messageId', m.id::text,
                'clientRequestId', m.client_request_id,
                'senderUserId', m.sender_user_id::text,
                'senderPeerId', $2,
                'targetUserId', $3,
                'text', m.body,
                'createdAt', m.created_at,
                'deliverTo', a.peer_id
            )
        )
        FROM active_sessions a
    ), '[]'::json)
)
FROM upsert_message m
LIMIT 1
)SQL";

        return querySingleJson(
            sql,
            {
                std::string(senderUserId),
                std::string(senderPeerId),
                std::string(targetUserId),
                std::string(clientRequestId),
                std::string(text),
                serverNode_
            },
            "Direct message sent.");
    }

    core::contracts::OperationStatus MessengerRepository::listDirectThreads(
        std::string_view requesterUserId,
        std::size_t limit) const {
        if (!isReady()) {
            return dbFailure("Repository is not ready.");
        }
        if (requesterUserId.empty()) {
            return dbFailure("listDirectThreads: requesterUserId must not be empty.");
        }

        const auto cappedLimit = clampBatchSize(limit, 1, 200);
        static const std::string sql = R"SQL(
WITH thread_rows AS (
    SELECT
        dt.id,
        dt.user_a,
        dt.user_b,
        dt.last_message_at,
        dt.updated_at,
        CASE
            WHEN dt.user_a = $1::uuid THEN dt.user_b::text
            ELSE dt.user_a::text
        END AS counterpart_user_id
      FROM app.direct_threads dt
     WHERE dt.user_a = $1::uuid
        OR dt.user_b = $1::uuid
     ORDER BY dt.last_message_at DESC NULLS LAST, dt.updated_at DESC, dt.id DESC
     LIMIT LEAST(GREATEST($2::int, 1), 200)
),
thread_payload AS (
    SELECT
        tr.id,
        tr.last_message_at,
        tr.updated_at,
        tr.counterpart_user_id,
        lm.message_id,
        lm.sender_user_id,
        lm.body,
        lm.body_type,
        lm.created_at AS last_message_created_at,
        COALESCE(u.unread_count, 0) AS unread_count
      FROM thread_rows tr
      LEFT JOIN LATERAL (
          SELECT
              m.id AS message_id,
              m.sender_user_id,
              m.body,
              m.body_type,
              m.created_at
            FROM app.direct_messages m
           WHERE m.thread_id = tr.id
             AND m.deleted_at IS NULL
           ORDER BY m.created_at DESC, m.id DESC
           LIMIT 1
      ) lm ON TRUE
      LEFT JOIN LATERAL (
          SELECT COUNT(*)::bigint AS unread_count
            FROM app.direct_messages m
            LEFT JOIN app.direct_message_receipts r
              ON r.message_id = m.id
             AND r.recipient_user_id = $1::uuid
           WHERE m.thread_id = tr.id
             AND m.sender_user_id <> $1::uuid
             AND m.deleted_at IS NULL
             AND r.read_at IS NULL
      ) u ON TRUE
)
SELECT json_build_object(
    'threads', COALESCE((
        SELECT json_agg(
            json_build_object(
                'threadId', tp.id::text,
                'counterpartUserId', tp.counterpart_user_id,
                'lastMessageAt', tp.last_message_at,
                'updatedAt', tp.updated_at,
                'unreadCount', tp.unread_count,
                'lastMessage', CASE
                    WHEN tp.message_id IS NULL THEN NULL
                    ELSE json_build_object(
                        'messageId', tp.message_id::text,
                        'senderUserId', tp.sender_user_id::text,
                        'text', tp.body,
                        'bodyType', tp.body_type,
                        'createdAt', tp.last_message_created_at
                    )
                END
            )
            ORDER BY tp.last_message_at DESC NULLS LAST, tp.updated_at DESC, tp.id DESC
        )
          FROM thread_payload tp
    ), '[]'::json)
)
)SQL";

        return querySingleJson(
            sql,
            {
                std::string(requesterUserId),
                std::to_string(cappedLimit)
            },
            "Direct threads listed.");
    }

    core::contracts::OperationStatus MessengerRepository::listDirectMessages(
        std::string_view requesterUserId,
        std::string_view targetUserId,
        std::string_view threadId,
        std::size_t limit,
        std::string_view beforeCreatedAt,
        std::string_view afterCreatedAt) const {
        if (!isReady()) {
            return dbFailure("Repository is not ready.");
        }
        if (requesterUserId.empty()) {
            return dbFailure("listDirectMessages: requesterUserId must not be empty.");
        }

        const auto cappedLimit = clampBatchSize(limit, 1, 200);
        static const std::string sql = R"SQL(
WITH target_thread AS (
    SELECT
        dt.id,
        dt.user_a,
        dt.user_b
      FROM app.direct_threads dt
     WHERE (
            NULLIF($3, '') IS NOT NULL
            AND dt.id::text = NULLIF($3, '')
            AND (dt.user_a = $1::uuid OR dt.user_b = $1::uuid)
       )
        OR (
            NULLIF($3, '') IS NULL
            AND NULLIF($2, '') IS NOT NULL
            AND (
                (dt.user_a = $1::uuid AND dt.user_b::text = NULLIF($2, ''))
                OR (dt.user_b = $1::uuid AND dt.user_a::text = NULLIF($2, ''))
            )
       )
     LIMIT 1
),
rows AS (
    SELECT
        m.id,
        m.thread_id,
        m.sender_user_id,
        m.body,
        m.body_type,
        m.client_request_id,
        m.created_at,
        m.edited_at,
        r.delivered_at AS recipient_delivered_at,
        r.read_at AS recipient_read_at
      FROM app.direct_messages m
      JOIN target_thread t
        ON t.id = m.thread_id
      LEFT JOIN app.direct_message_receipts r
        ON r.message_id = m.id
       AND r.recipient_user_id = $1::uuid
     WHERE m.deleted_at IS NULL
       AND (
            NULLIF($5, '') IS NULL
            OR m.created_at < NULLIF($5, '')::timestamptz
       )
       AND (
            NULLIF($6, '') IS NULL
            OR m.created_at > NULLIF($6, '')::timestamptz
       )
     ORDER BY m.created_at DESC, m.id DESC
     LIMIT LEAST(GREATEST($4::int, 1), 200) + 1
),
paged AS (
    SELECT *
      FROM rows
     ORDER BY created_at DESC, id DESC
     LIMIT LEAST(GREATEST($4::int, 1), 200)
),
has_more AS (
    SELECT COUNT(*) > LEAST(GREATEST($4::int, 1), 200) AS value
      FROM rows
)
SELECT COALESCE(
    (
        SELECT json_build_object(
            'threadId', t.id::text,
            'counterpartUserId', CASE
                WHEN t.user_a = $1::uuid THEN t.user_b::text
                ELSE t.user_a::text
            END,
            'messages', COALESCE((
                SELECT json_agg(
                    json_build_object(
                        'messageId', p.id::text,
                        'threadId', p.thread_id::text,
                        'senderUserId', p.sender_user_id::text,
                        'text', p.body,
                        'bodyType', p.body_type,
                        'clientRequestId', p.client_request_id,
                        'createdAt', p.created_at,
                        'editedAt', p.edited_at,
                        'recipientDeliveredAt', p.recipient_delivered_at,
                        'recipientReadAt', p.recipient_read_at
                    )
                    ORDER BY p.created_at DESC, p.id DESC
                )
                  FROM paged p
            ), '[]'::json),
            'hasMore', COALESCE((SELECT value FROM has_more), false),
            'nextBeforeCreatedAt', (
                SELECT p.created_at
                  FROM paged p
                 ORDER BY p.created_at ASC, p.id ASC
                 LIMIT 1
            )
        )
          FROM target_thread t
         LIMIT 1
    ),
    json_build_object(
        'threadId', NULLIF($3, ''),
        'counterpartUserId', NULLIF($2, ''),
        'messages', '[]'::json,
        'hasMore', false,
        'nextBeforeCreatedAt', NULL
    )
)
)SQL";

        return querySingleJson(
            sql,
            {
                std::string(requesterUserId),
                std::string(targetUserId),
                std::string(threadId),
                std::to_string(cappedLimit),
                std::string(beforeCreatedAt),
                std::string(afterCreatedAt)
            },
            "Direct messages synced.");
    }

    core::contracts::OperationStatus MessengerRepository::ackDirectMessages(
        std::string_view requesterUserId,
        std::string_view targetUserId,
        std::string_view threadId,
        const std::vector<std::string>& messageIds,
        bool markRead) {
        if (!isReady()) {
            return dbFailure("Repository is not ready.");
        }
        if (requesterUserId.empty()) {
            return dbFailure("ackDirectMessages: requesterUserId must not be empty.");
        }

        const auto messageIdsJson = encodeJsonArray(messageIds);
        static const std::string sql = R"SQL(
WITH target_thread AS (
    SELECT
        dt.id,
        dt.user_a,
        dt.user_b
      FROM app.direct_threads dt
     WHERE (
            NULLIF($3, '') IS NOT NULL
            AND dt.id::text = NULLIF($3, '')
            AND (dt.user_a = $1::uuid OR dt.user_b = $1::uuid)
       )
        OR (
            NULLIF($3, '') IS NULL
            AND NULLIF($2, '') IS NOT NULL
            AND (
                (dt.user_a = $1::uuid AND dt.user_b::text = NULLIF($2, ''))
                OR (dt.user_b = $1::uuid AND dt.user_a::text = NULLIF($2, ''))
            )
       )
     LIMIT 1
),
ids AS (
    SELECT DISTINCT value::uuid AS message_id
      FROM jsonb_array_elements_text(COALESCE(NULLIF($4, '')::jsonb, '[]'::jsonb))
     WHERE value ~* '^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$'
),
upsert_receipts AS (
    INSERT INTO app.direct_message_receipts (
        message_id,
        recipient_user_id,
        delivered_at,
        read_at
    )
    SELECT
        m.id,
        $1::uuid,
        timezone('utc', now()),
        CASE
            WHEN $5::boolean THEN timezone('utc', now())
            ELSE NULL
        END
      FROM ids i
      JOIN target_thread t ON TRUE
      JOIN app.direct_messages m
        ON m.id = i.message_id
       AND m.thread_id = t.id
     WHERE m.sender_user_id <> $1::uuid
    ON CONFLICT (message_id, recipient_user_id) DO UPDATE
        SET delivered_at = COALESCE(
                app.direct_message_receipts.delivered_at,
                EXCLUDED.delivered_at
            ),
            read_at = CASE
                WHEN $5::boolean THEN COALESCE(
                    app.direct_message_receipts.read_at,
                    EXCLUDED.read_at
                )
                ELSE app.direct_message_receipts.read_at
            END
    RETURNING message_id
)
SELECT json_build_object(
    'threadId', t.id::text,
    'ackedCount', (SELECT COUNT(*) FROM upsert_receipts),
    'ackedMessageIds', COALESCE((
        SELECT json_agg(message_id::text)
          FROM upsert_receipts
    ), '[]'::json),
    'markRead', $5::boolean
)
FROM target_thread t
LIMIT 1
)SQL";

        return querySingleJson(
            sql,
            {
                std::string(requesterUserId),
                std::string(targetUserId),
                std::string(threadId),
                messageIdsJson,
                markRead ? "true" : "false"
            },
            "Direct messages acknowledged.");
    }

    core::contracts::OperationStatus MessengerRepository::searchUsersByEmail(
        std::string_view requesterUserId,
        std::string_view query,
        std::size_t limit) const {
        if (!isReady()) {
            return dbFailure("Repository is not ready.");
        }
        if (requesterUserId.empty()) {
            return dbFailure("searchUsersByEmail: requesterUserId must not be empty.");
        }

        auto normalizedQuery = std::string(query);
        const auto first = normalizedQuery.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            json payload = json::object();
            payload["query"] = "";
            payload["users"] = json::array();
            return core::contracts::OperationStatus::success("Users searched.", std::move(payload));
        }
        const auto last = normalizedQuery.find_last_not_of(" \t\r\n");
        normalizedQuery = normalizedQuery.substr(first, last - first + 1);

        const auto cappedLimit = clampBatchSize(limit, 1, 100);
        static const std::string sql = R"SQL(
WITH input AS (
    SELECT lower(NULLIF(trim($2), '')) AS q
),
profile_matches AS (
    SELECT
        up.user_id::text AS user_id,
        lower(trim(up.email::text)) AS email,
        NULLIF(trim(up.display_name), '') AS display_name
      FROM app.user_profiles up
      CROSS JOIN input i
     WHERE i.q IS NOT NULL
       AND up.user_id <> $1::uuid
       AND up.email IS NOT NULL
       AND trim(up.email::text) <> ''
       AND position(i.q IN lower(up.email::text)) > 0
),
auth_matches AS (
    SELECT
        au.id::text AS user_id,
        lower(trim(au.email::text)) AS email,
        NULLIF(trim(up.display_name), '') AS display_name
      FROM auth.users au
      LEFT JOIN app.user_profiles up
        ON up.user_id = au.id
      CROSS JOIN input i
     WHERE i.q IS NOT NULL
       AND au.id <> $1::uuid
       AND au.deleted_at IS NULL
       AND au.email IS NOT NULL
       AND trim(au.email::text) <> ''
       AND position(i.q IN lower(au.email::text)) > 0
),
merged AS (
    SELECT user_id, email, display_name FROM profile_matches
    UNION
    SELECT user_id, email, display_name FROM auth_matches
),
ranked AS (
    SELECT
        m.user_id,
        m.email,
        m.display_name,
        CASE
            WHEN m.email = i.q THEN 0
            WHEN left(m.email, char_length(i.q)) = i.q THEN 1
            ELSE 2
        END AS rank
      FROM merged m
      CROSS JOIN input i
),
deduped AS (
    SELECT DISTINCT ON (r.user_id)
        r.user_id,
        r.email,
        r.display_name,
        r.rank
      FROM ranked r
     ORDER BY r.user_id, r.rank, r.email
),
limited AS (
    SELECT
        d.user_id,
        d.email,
        d.display_name,
        d.rank
      FROM deduped d
     ORDER BY d.rank, d.email
     LIMIT LEAST(GREATEST($3::int, 1), 100)
)
SELECT json_build_object(
    'query', NULLIF(trim($2), ''),
    'users', COALESCE((
        SELECT json_agg(
            json_build_object(
                'userId', l.user_id,
                'email', l.email,
                'displayName', l.display_name
            )
            ORDER BY l.rank, l.email
        )
          FROM limited l
    ), '[]'::json)
)
)SQL";

        return querySingleJson(
            sql,
            {
                std::string(requesterUserId),
                std::move(normalizedQuery),
                std::to_string(cappedLimit)
            },
            "Users searched.");
    }

    core::contracts::OperationStatus MessengerRepository::createDirectCall(
        std::string_view callerUserId,
        std::string_view callerPeerId,
        std::string_view calleeUserId,
        std::string_view clientRequestId) {
        if (!isReady()) {
            return dbFailure("Repository is not ready.");
        }
        if (callerUserId.empty() || callerPeerId.empty() || calleeUserId.empty()) {
            return dbFailure("createDirectCall: callerUserId, callerPeerId and calleeUserId must not be empty.");
        }
        if (callerUserId == calleeUserId) {
            return dbFailure("createDirectCall: self call is not allowed.");
        }

        static const std::string sql = R"SQL(
WITH caller_session AS (
    SELECT us.id
      FROM app.user_sessions us
     WHERE us.peer_id = $2
       AND us.status = 'connected'
     LIMIT 1
),
upsert_call AS (
    INSERT INTO app.calls (
        public_id,
        media_room_id,
        call_kind,
        initiator_user_id,
        status,
        client_request_id,
        metadata
    )
    VALUES (
        'call_' || replace(gen_random_uuid()::text, '-', ''),
        'dm_' || replace(gen_random_uuid()::text, '-', ''),
        'direct',
        $1::uuid,
        'invited',
        NULLIF($4, ''),
        jsonb_build_object(
            'callerPeerId', $2,
            'calleeUserId', $3::uuid::text
        )
    )
    ON CONFLICT (initiator_user_id, client_request_id) WHERE client_request_id IS NOT NULL
    DO UPDATE SET status = app.calls.status
    RETURNING id, public_id, media_room_id, status
),
caller_participant AS (
    INSERT INTO app.call_participants (
        call_id,
        user_id,
        role,
        participant_status,
        invited_session_id,
        joined_session_id,
        invited_at,
        answered_at,
        metadata
    )
    SELECT
        c.id,
        $1::uuid,
        'caller',
        'accepted',
        s.id,
        s.id,
        timezone('utc', now()),
        timezone('utc', now()),
        jsonb_build_object('peerId', $2)
    FROM upsert_call c
    CROSS JOIN caller_session s
    ON CONFLICT (call_id, user_id) DO NOTHING
    RETURNING call_id
),
callee_participant AS (
    INSERT INTO app.call_participants (
        call_id,
        user_id,
        role,
        participant_status,
        invited_at,
        metadata
    )
    SELECT
        c.id,
        $3::uuid,
        'callee',
        'invited',
        timezone('utc', now()),
        jsonb_build_object('invitedByPeerId', $2)
    FROM upsert_call c
    ON CONFLICT (call_id, user_id) DO NOTHING
    RETURNING call_id
),
alloc AS (
    INSERT INTO app.media_room_allocations (
        media_room_id,
        owner_object_type,
        owner_object_id,
        room_id,
        owner_type,
        owner_public_id,
        backend_engine,
        backend_node,
        metadata
    )
    SELECT
        c.media_room_id,
        'direct_call',
        c.id,
        c.media_room_id,
        'direct_call',
        c.public_id,
        'mediasoup',
        $5,
        jsonb_build_object(
            'callPublicId', c.public_id,
            'initiatorUserId', $1::uuid::text
        )
    FROM upsert_call c
    ON CONFLICT (owner_type, owner_public_id) DO UPDATE
        SET media_room_id = EXCLUDED.media_room_id,
            owner_object_type = EXCLUDED.owner_object_type,
            owner_object_id = EXCLUDED.owner_object_id,
            room_id = EXCLUDED.room_id,
            backend_engine = EXCLUDED.backend_engine,
            backend_node = EXCLUDED.backend_node,
            metadata = app.media_room_allocations.metadata || EXCLUDED.metadata
    RETURNING room_id
),
event_ins AS (
    INSERT INTO app.call_events (
        call_id,
        actor_user_id,
        actor_session_id,
        event_type,
        payload
    )
    SELECT
        c.id,
        $1::uuid,
        s.id,
        'invite_created',
        jsonb_build_object(
            'callerPeerId', $2,
            'calleeUserId', $3::uuid::text
        )
    FROM upsert_call c
    CROSS JOIN caller_session s
    RETURNING id
),
callee_active_sessions AS (
    SELECT us.id AS session_id, us.peer_id, us.user_id, us.server_node
      FROM app.user_sessions us
     WHERE us.user_id = $3::uuid
       AND us.status = 'connected'
),
offline_callee AS (
    SELECT $3::uuid AS user_id
     WHERE NOT EXISTS (SELECT 1 FROM callee_active_sessions)
),
outbox AS (
    INSERT INTO app.realtime_outbox (
        server_node,
        target_user_id,
        target_session_id,
        target_peer_id,
        recipient_user_id,
        recipient_session_id,
        aggregate_type,
        aggregate_id,
        event_type,
        payload,
        status
    )
    SELECT
        s.server_node,
        s.user_id,
        s.session_id,
        s.peer_id,
        s.user_id,
        s.session_id,
        'direct_call',
        c.public_id,
        'direct_call_invite',
        jsonb_build_object(
            'type', 'direct_call_invite',
            'object', 'direct_call',
            'callId', c.public_id,
            'roomId', c.media_room_id,
            'callerUserId', $1::uuid::text,
            'callerPeerId', $2,
            'targetUserId', $3::uuid::text,
            'status', c.status
        ),
        'pending'
    FROM upsert_call c
    CROSS JOIN callee_active_sessions s
    RETURNING id
),
offline_outbox AS (
    INSERT INTO app.realtime_outbox (
        server_node,
        target_user_id,
        target_session_id,
        target_peer_id,
        recipient_user_id,
        recipient_session_id,
        aggregate_type,
        aggregate_id,
        event_type,
        payload,
        status
    )
    SELECT
        $5,
        o.user_id,
        NULL,
        NULL,
        o.user_id,
        NULL,
        'direct_call',
        c.public_id,
        'direct_call_invite',
        jsonb_build_object(
            'type', 'direct_call_invite',
            'object', 'direct_call',
            'callId', c.public_id,
            'roomId', c.media_room_id,
            'callerUserId', $1::uuid::text,
            'callerPeerId', $2,
            'targetUserId', $3::uuid::text,
            'status', c.status
        ),
        'pending'
    FROM upsert_call c
    CROSS JOIN offline_callee o
    RETURNING id
)
SELECT json_build_object(
    'callId', c.public_id,
    'roomId', c.media_room_id,
    'status', c.status,
    'outboundEvents', COALESCE((
        SELECT json_agg(
            json_build_object(
                'type', 'direct_call_invite',
                'object', 'direct_call',
                'callId', c.public_id,
                'roomId', c.media_room_id,
                'callerUserId', $1::uuid::text,
                'callerPeerId', $2,
                'targetUserId', $3::uuid::text,
                'status', c.status,
                'deliverTo', s.peer_id
            )
        )
        FROM callee_active_sessions s
    ), '[]'::json)
)
FROM upsert_call c
LIMIT 1
)SQL";

        return querySingleJson(
            sql,
            {
                std::string(callerUserId),
                std::string(callerPeerId),
                std::string(calleeUserId),
                std::string(clientRequestId),
                serverNode_
            },
            "Direct call created.");
    }

    core::contracts::OperationStatus MessengerRepository::listUserActiveDirectCalls(
        std::string_view userId,
        std::size_t limit) const {
        if (!isReady()) {
            return dbFailure("Repository is not ready.");
        }
        if (userId.empty()) {
            return dbFailure("listUserActiveDirectCalls: userId must not be empty.");
        }

        const auto cappedLimit = clampBatchSize(limit, 1, 200);
        static const std::string sql = R"SQL(
WITH scoped_calls AS (
    SELECT
        c.id,
        c.public_id,
        c.media_room_id,
        c.status,
        c.initiator_user_id,
        c.started_at,
        c.answered_at,
        c.ended_at
      FROM app.calls c
      JOIN app.call_participants self_p
        ON self_p.call_id = c.id
       AND self_p.user_id = $1::uuid
     WHERE c.status IN ('invited', 'ringing', 'accepted')
     ORDER BY c.started_at DESC, c.id DESC
     LIMIT LEAST(GREATEST($2::int, 1), 200)
)
SELECT json_build_object(
    'calls', COALESCE((
        SELECT json_agg(
            json_build_object(
                'callId', sc.public_id,
                'roomId', sc.media_room_id,
                'status', sc.status,
                'initiatorUserId', sc.initiator_user_id::text,
                'startedAt', sc.started_at,
                'answeredAt', sc.answered_at,
                'endedAt', sc.ended_at,
                'participants', COALESCE((
                    SELECT json_agg(
                        json_build_object(
                            'userId', p.user_id::text,
                            'role', p.role,
                            'participantStatus', p.participant_status,
                            'invitedAt', p.invited_at,
                            'answeredAt', p.answered_at,
                            'leftAt', p.left_at,
                            'endReason', p.end_reason
                        )
                        ORDER BY p.role DESC, p.invited_at ASC
                    )
                      FROM app.call_participants p
                     WHERE p.call_id = sc.id
                ), '[]'::json),
                'participantPeerIds', COALESCE((
                    SELECT json_agg(DISTINCT us.peer_id)
                      FROM app.call_participants p
                      JOIN app.user_sessions us
                        ON us.user_id = p.user_id
                       AND us.status = 'connected'
                     WHERE p.call_id = sc.id
                ), '[]'::json)
            )
            ORDER BY sc.started_at DESC, sc.id DESC
        )
          FROM scoped_calls sc
    ), '[]'::json)
)
)SQL";

        return querySingleJson(
            sql,
            {
                std::string(userId),
                std::to_string(cappedLimit)
            },
            "Active direct calls listed.");
    }

    core::contracts::OperationStatus MessengerRepository::listUserConferences(
        std::string_view userId,
        std::size_t limit) const {
        if (!isReady()) {
            return dbFailure("Repository is not ready.");
        }
        if (userId.empty()) {
            return dbFailure("listUserConferences: userId must not be empty.");
        }

        const auto cappedLimit = clampBatchSize(limit, 1, 200);
        static const std::string sql = R"SQL(
WITH scoped_conferences AS (
    SELECT
        c.id,
        c.public_id,
        c.owner_user_id,
        c.title,
        c.conference_kind,
        c.status,
        c.created_at,
        c.updated_at,
        c.closed_at,
        COALESCE(
            m.role,
            CASE
                WHEN c.owner_user_id = $1::uuid THEN 'owner'
                ELSE 'member'
            END
        ) AS role,
        COALESCE(
            m.membership_status,
            CASE
                WHEN c.owner_user_id = $1::uuid THEN 'joined'
                ELSE 'joined'
            END
        ) AS membership_status,
        m.joined_at,
        m.left_at
      FROM app.conferences c
      LEFT JOIN app.conference_members m
        ON m.conference_id = c.id
       AND m.user_id = $1::uuid
     WHERE c.owner_user_id = $1::uuid
        OR m.membership_status IN ('invited', 'joined', 'left')
     ORDER BY c.updated_at DESC, c.created_at DESC, c.id DESC
     LIMIT LEAST(GREATEST($2::int, 1), 200)
)
SELECT json_build_object(
    'conferences', COALESCE((
        SELECT json_agg(
            json_build_object(
                'conferencePublicId', sc.public_id,
                'ownerUserId', sc.owner_user_id::text,
                'title', sc.title,
                'conferenceKind', sc.conference_kind,
                'status', sc.status,
                'role', sc.role,
                'membershipStatus', sc.membership_status,
                'createdAt', sc.created_at,
                'updatedAt', sc.updated_at,
                'closedAt', sc.closed_at,
                'joinedAt', sc.joined_at,
                'leftAt', sc.left_at,
                'mediaRoomId', (
                    SELECT ma.room_id
                      FROM app.media_room_allocations ma
                     WHERE ma.owner_type = 'conference'
                       AND ma.owner_public_id = sc.public_id
                     LIMIT 1
                ),
                'activePeerIds', COALESCE((
                    SELECT json_agg(DISTINCT us.peer_id)
                      FROM app.conference_sessions cs
                      JOIN app.user_sessions us
                        ON us.id = cs.session_id
                       AND us.status = 'connected'
                     WHERE cs.conference_id = sc.id
                       AND cs.left_at IS NULL
                ), '[]'::json),
                'lastMessageAt', (
                    SELECT MAX(cm.created_at)
                      FROM app.conference_messages cm
                     WHERE cm.conference_id = sc.id
                       AND cm.deleted_at IS NULL
                )
            )
            ORDER BY sc.updated_at DESC, sc.created_at DESC, sc.id DESC
        )
          FROM scoped_conferences sc
    ), '[]'::json)
)
)SQL";

        return querySingleJson(
            sql,
            {
                std::string(userId),
                std::to_string(cappedLimit)
            },
            "User conferences listed.");
    }

    core::contracts::OperationStatus MessengerRepository::claimPendingOfflineOutbox(std::size_t limit) {
        if (!isReady()) {
            return dbFailure("Repository is not ready.");
        }

        const auto cappedLimit = clampBatchSize(limit, 1, 100);
        static const std::string sql = R"SQL(
WITH candidates AS (
    SELECT ro.id
      FROM app.realtime_outbox ro
     WHERE ro.server_node = $1
       AND ro.status IN ('pending', 'retry')
       AND ro.available_at <= timezone('utc', now())
       AND ro.target_session_id IS NULL
       AND ro.target_peer_id IS NULL
     ORDER BY ro.id
     FOR UPDATE SKIP LOCKED
     LIMIT $2::int
),
claimed AS (
    UPDATE app.realtime_outbox ro
       SET status = 'dispatching',
           attempts = ro.attempts + 1,
           last_error = NULL
      FROM candidates c
     WHERE ro.id = c.id
     RETURNING
        ro.id,
        ro.event_type,
        ro.payload,
        COALESCE(ro.recipient_user_id::text, ro.target_user_id::text, '') AS recipient_user_id,
        COALESCE(ro.target_peer_id, '') AS target_peer_id,
        ro.attempts
)
SELECT json_build_object(
    'events', COALESCE((
        SELECT json_agg(
            json_build_object(
                'id', c.id,
                'eventType', c.event_type,
                'payload', c.payload,
                'recipientUserId', NULLIF(c.recipient_user_id, ''),
                'targetPeerId', NULLIF(c.target_peer_id, ''),
                'attempts', c.attempts
            )
            ORDER BY c.id
        )
          FROM claimed c
    ), '[]'::json)
)
)SQL";

        return querySingleJson(
            sql,
            {
                serverNode_,
                std::to_string(cappedLimit)
            },
            "Pending offline outbox claimed.");
    }

    core::contracts::OperationStatus MessengerRepository::markOfflineOutboxDelivered(std::int64_t outboxId) {
        if (!isReady()) {
            return dbFailure("Repository is not ready.");
        }
        if (outboxId <= 0) {
            return dbFailure("markOfflineOutboxDelivered: outboxId must be positive.");
        }

        static const std::string sql = R"SQL(
WITH updated AS (
    UPDATE app.realtime_outbox ro
       SET status = 'delivered',
           delivered_at = timezone('utc', now()),
           last_error = NULL
     WHERE ro.id = $1::bigint
       AND ro.status = 'dispatching'
     RETURNING
        ro.id,
        ro.event_type,
        ro.payload,
        COALESCE(ro.recipient_user_id, ro.target_user_id) AS recipient_user_id
),
conference_receipts AS (
    UPDATE app.conference_message_receipts r
       SET delivered_at = COALESCE(r.delivered_at, timezone('utc', now()))
      FROM updated u
     WHERE u.event_type = 'chat_message'
       AND u.recipient_user_id IS NOT NULL
       AND (u.payload ? 'messageId')
       AND (u.payload ->> 'messageId') ~* '^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$'
       AND r.message_id = (u.payload ->> 'messageId')::uuid
       AND r.recipient_user_id = u.recipient_user_id
    RETURNING r.message_id
),
direct_receipts AS (
    UPDATE app.direct_message_receipts r
       SET delivered_at = COALESCE(r.delivered_at, timezone('utc', now()))
      FROM updated u
     WHERE u.event_type = 'direct_chat_message'
       AND u.recipient_user_id IS NOT NULL
       AND (u.payload ? 'messageId')
       AND (u.payload ->> 'messageId') ~* '^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$'
       AND r.message_id = (u.payload ->> 'messageId')::uuid
       AND r.recipient_user_id = u.recipient_user_id
    RETURNING r.message_id
)
SELECT 1
)SQL";

        return executeOnly(
            sql,
            { std::to_string(outboxId) },
            "Offline outbox marked as delivered.");
    }

    core::contracts::OperationStatus MessengerRepository::markOfflineOutboxRetry(
        std::int64_t outboxId,
        std::string_view reason,
        std::int32_t maxAttempts) {
        if (!isReady()) {
            return dbFailure("Repository is not ready.");
        }
        if (outboxId <= 0) {
            return dbFailure("markOfflineOutboxRetry: outboxId must be positive.");
        }
        if (maxAttempts <= 0) {
            maxAttempts = 1;
        }

        static const std::string sql = R"SQL(
UPDATE app.realtime_outbox ro
   SET status = CASE
           WHEN ro.attempts >= $2::int THEN 'error'
           ELSE 'retry'
       END,
       last_error = NULLIF(left($3, 1024), ''),
       available_at = CASE
           WHEN ro.attempts >= $2::int THEN ro.available_at
           ELSE timezone('utc', now()) + make_interval(
               secs => LEAST(
                   300,
                   GREATEST(
                       1,
                       CAST(power(2::numeric, LEAST(ro.attempts, 8)) AS integer)
                   )
               )
           )
       END
 WHERE ro.id = $1::bigint
   AND ro.status = 'dispatching'
)SQL";

        return executeOnly(
            sql,
            {
                std::to_string(outboxId),
                std::to_string(maxAttempts),
                std::string(reason)
            },
            "Offline outbox scheduled for retry.");
    }

    core::contracts::OperationStatus MessengerRepository::resolveDirectCallMediaContext(
        std::string_view actorUserId,
        std::string_view callPublicId) const {
        if (!isReady()) {
            return dbFailure("Repository is not ready.");
        }
        if (actorUserId.empty() || callPublicId.empty()) {
            return dbFailure("resolveDirectCallMediaContext: actorUserId and callPublicId must not be empty.");
        }

        static const std::string sql = R"SQL(
WITH target AS (
    SELECT c.id, c.public_id, c.media_room_id, c.status
      FROM app.calls c
      JOIN app.call_participants p
        ON p.call_id = c.id
       AND p.user_id = $1::uuid
     WHERE c.public_id = $2
     LIMIT 1
),
participant_peers AS (
    SELECT DISTINCT us.peer_id
      FROM target t
      JOIN app.call_participants p
        ON p.call_id = t.id
      JOIN app.user_sessions us
        ON us.user_id = p.user_id
       AND us.status = 'connected'
)
SELECT json_build_object(
    'callId', t.public_id,
    'roomId', t.media_room_id,
    'status', t.status,
    'participantPeerIds', COALESCE((SELECT json_agg(peer_id) FROM participant_peers), '[]'::json)
)
FROM target t
LIMIT 1
)SQL";

        return querySingleJson(
            sql,
            {
                std::string(actorUserId),
                std::string(callPublicId)
            },
            "Direct call media context resolved.");
    }

    core::contracts::OperationStatus MessengerRepository::acceptDirectCall(
        std::string_view calleeUserId,
        std::string_view calleePeerId,
        std::string_view callPublicId) {
        if (!isReady()) {
            return dbFailure("Repository is not ready.");
        }
        if (calleeUserId.empty() || calleePeerId.empty() || callPublicId.empty()) {
            return dbFailure("acceptDirectCall: calleeUserId, calleePeerId and callPublicId must not be empty.");
        }

        static const std::string sql = R"SQL(
WITH callee_session AS (
    SELECT us.id
      FROM app.user_sessions us
     WHERE us.peer_id = $2
       AND us.status = 'connected'
     LIMIT 1
),
target AS (
    SELECT c.id, c.public_id, c.media_room_id
      FROM app.calls c
      JOIN app.call_participants p
        ON p.call_id = c.id
       AND p.user_id = $1::uuid
     WHERE c.public_id = $3
       AND c.status IN ('invited', 'ringing')
     LIMIT 1
),
update_call AS (
    UPDATE app.calls c
       SET status = 'accepted',
           answered_at = COALESCE(c.answered_at, timezone('utc', now()))
      FROM target t
     WHERE c.id = t.id
     RETURNING c.id, c.public_id, c.media_room_id, c.status
),
update_callee AS (
    UPDATE app.call_participants p
       SET participant_status = 'accepted',
           joined_session_id = s.id,
           answered_at = COALESCE(p.answered_at, timezone('utc', now())),
           metadata = p.metadata || jsonb_build_object('acceptedPeerId', $2)
      FROM update_call c
      CROSS JOIN callee_session s
     WHERE p.call_id = c.id
       AND p.user_id = $1::uuid
     RETURNING p.call_id
),
event_ins AS (
    INSERT INTO app.call_events (
        call_id,
        actor_user_id,
        actor_session_id,
        event_type,
        payload
    )
    SELECT
        c.id,
        $1::uuid,
        s.id,
        'accepted',
        jsonb_build_object('peerId', $2)
    FROM update_call c
    CROSS JOIN callee_session s
    RETURNING id
),
participant_sessions AS (
    SELECT DISTINCT us.peer_id
      FROM app.call_participants p
      JOIN app.user_sessions us
        ON us.user_id = p.user_id
       AND us.status = 'connected'
      JOIN update_call c
        ON c.id = p.call_id
),
outbox AS (
    INSERT INTO app.realtime_outbox (
        server_node,
        target_user_id,
        target_session_id,
        target_peer_id,
        recipient_user_id,
        recipient_session_id,
        aggregate_type,
        aggregate_id,
        event_type,
        payload,
        status
    )
    SELECT
        us.server_node,
        us.user_id,
        us.id,
        us.peer_id,
        us.user_id,
        us.id,
        'direct_call',
        c.public_id,
        'direct_call_accepted',
        jsonb_build_object(
            'type', 'direct_call_accepted',
            'object', 'direct_call',
            'callId', c.public_id,
            'roomId', c.media_room_id,
            'calleeUserId', $1::uuid::text,
            'calleePeerId', $2,
            'status', c.status
        ),
        'pending'
    FROM update_call c
    JOIN app.user_sessions us
      ON us.peer_id IN (SELECT peer_id FROM participant_sessions)
    RETURNING id
)
SELECT json_build_object(
    'callId', c.public_id,
    'roomId', c.media_room_id,
    'status', c.status,
    'participantPeerIds', COALESCE((SELECT json_agg(peer_id) FROM participant_sessions), '[]'::json),
    'outboundEvents', COALESCE((
        SELECT json_agg(
            json_build_object(
                'type', 'direct_call_accepted',
                'object', 'direct_call',
                'callId', c.public_id,
                'roomId', c.media_room_id,
                'calleeUserId', $1::uuid::text,
                'calleePeerId', $2,
                'status', c.status,
                'deliverTo', peer_id
            )
        )
        FROM participant_sessions
    ), '[]'::json)
)
FROM update_call c
LIMIT 1
)SQL";

        return querySingleJson(
            sql,
            {
                std::string(calleeUserId),
                std::string(calleePeerId),
                std::string(callPublicId)
            },
            "Direct call accepted.");
    }

    core::contracts::OperationStatus MessengerRepository::declineDirectCall(
        std::string_view calleeUserId,
        std::string_view calleePeerId,
        std::string_view callPublicId) {
        if (!isReady()) {
            return dbFailure("Repository is not ready.");
        }
        if (calleeUserId.empty() || calleePeerId.empty() || callPublicId.empty()) {
            return dbFailure("declineDirectCall: calleeUserId, calleePeerId and callPublicId must not be empty.");
        }

        static const std::string sql = R"SQL(
WITH callee_session AS (
    SELECT us.id
      FROM app.user_sessions us
     WHERE us.peer_id = $2
       AND us.status = 'connected'
     LIMIT 1
),
target AS (
    SELECT c.id, c.public_id, c.media_room_id
      FROM app.calls c
      JOIN app.call_participants p
        ON p.call_id = c.id
       AND p.user_id = $1::uuid
     WHERE c.public_id = $3
       AND c.status IN ('invited', 'ringing')
     LIMIT 1
),
update_call AS (
    UPDATE app.calls c
       SET status = 'declined',
           ended_at = COALESCE(c.ended_at, timezone('utc', now()))
      FROM target t
     WHERE c.id = t.id
     RETURNING c.id, c.public_id, c.media_room_id, c.status
),
update_participants AS (
    UPDATE app.call_participants p
       SET participant_status = CASE
               WHEN p.user_id = $1::uuid THEN 'declined'
               ELSE 'ended'
           END,
           left_at = timezone('utc', now()),
           end_reason = CASE
               WHEN p.user_id = $1::uuid THEN 'declined'
               ELSE 'callee_declined'
           END
      FROM update_call c
     WHERE p.call_id = c.id
     RETURNING p.user_id
),
event_ins AS (
    INSERT INTO app.call_events (
        call_id,
        actor_user_id,
        actor_session_id,
        event_type,
        payload
    )
    SELECT
        c.id,
        $1::uuid,
        s.id,
        'declined',
        jsonb_build_object('peerId', $2)
    FROM update_call c
    CROSS JOIN callee_session s
    RETURNING id
),
participant_sessions AS (
    SELECT DISTINCT us.peer_id
      FROM app.call_participants p
      JOIN app.user_sessions us
        ON us.user_id = p.user_id
       AND us.status = 'connected'
      JOIN update_call c
        ON c.id = p.call_id
),
outbox AS (
    INSERT INTO app.realtime_outbox (
        server_node,
        target_user_id,
        target_session_id,
        target_peer_id,
        recipient_user_id,
        recipient_session_id,
        aggregate_type,
        aggregate_id,
        event_type,
        payload,
        status
    )
    SELECT
        us.server_node,
        us.user_id,
        us.id,
        us.peer_id,
        us.user_id,
        us.id,
        'direct_call',
        c.public_id,
        'direct_call_declined',
        jsonb_build_object(
            'type', 'direct_call_declined',
            'object', 'direct_call',
            'callId', c.public_id,
            'roomId', c.media_room_id,
            'calleeUserId', $1::uuid::text,
            'calleePeerId', $2,
            'status', c.status
        ),
        'pending'
    FROM update_call c
    JOIN app.user_sessions us
      ON us.peer_id IN (SELECT peer_id FROM participant_sessions)
    RETURNING id
)
SELECT json_build_object(
    'callId', c.public_id,
    'roomId', c.media_room_id,
    'status', c.status,
    'participantPeerIds', COALESCE((SELECT json_agg(peer_id) FROM participant_sessions), '[]'::json),
    'outboundEvents', COALESCE((
        SELECT json_agg(
            json_build_object(
                'type', 'direct_call_declined',
                'object', 'direct_call',
                'callId', c.public_id,
                'roomId', c.media_room_id,
                'calleeUserId', $1::uuid::text,
                'calleePeerId', $2,
                'status', c.status,
                'deliverTo', peer_id
            )
        )
        FROM participant_sessions
    ), '[]'::json)
)
FROM update_call c
LIMIT 1
)SQL";

        return querySingleJson(
            sql,
            {
                std::string(calleeUserId),
                std::string(calleePeerId),
                std::string(callPublicId)
            },
            "Direct call declined.");
    }

    core::contracts::OperationStatus MessengerRepository::hangupDirectCall(
        std::string_view actorUserId,
        std::string_view actorPeerId,
        std::string_view callPublicId) {
        if (!isReady()) {
            return dbFailure("Repository is not ready.");
        }
        if (actorUserId.empty() || actorPeerId.empty() || callPublicId.empty()) {
            return dbFailure("hangupDirectCall: actorUserId, actorPeerId and callPublicId must not be empty.");
        }

        static const std::string sql = R"SQL(
WITH actor_session AS (
    SELECT us.id
      FROM app.user_sessions us
     WHERE us.peer_id = $2
       AND us.status = 'connected'
     LIMIT 1
),
target AS (
    SELECT c.id, c.public_id, c.media_room_id
      FROM app.calls c
      JOIN app.call_participants p
        ON p.call_id = c.id
       AND p.user_id = $1::uuid
     WHERE c.public_id = $3
       AND c.status IN ('invited', 'ringing', 'accepted')
     LIMIT 1
),
update_call AS (
    UPDATE app.calls c
       SET status = 'ended',
           ended_at = COALESCE(c.ended_at, timezone('utc', now()))
      FROM target t
     WHERE c.id = t.id
     RETURNING c.id, c.public_id, c.media_room_id, c.status
),
update_participants AS (
    UPDATE app.call_participants p
       SET participant_status = 'ended',
           left_at = COALESCE(p.left_at, timezone('utc', now())),
           end_reason = COALESCE(p.end_reason, 'hangup')
      FROM update_call c
     WHERE p.call_id = c.id
     RETURNING p.user_id
),
event_ins AS (
    INSERT INTO app.call_events (
        call_id,
        actor_user_id,
        actor_session_id,
        event_type,
        payload
    )
    SELECT
        c.id,
        $1::uuid,
        s.id,
        'ended',
        jsonb_build_object('peerId', $2)
    FROM update_call c
    CROSS JOIN actor_session s
    RETURNING id
),
participant_sessions AS (
    SELECT DISTINCT us.peer_id
      FROM app.call_participants p
      JOIN app.user_sessions us
        ON us.user_id = p.user_id
       AND us.status = 'connected'
      JOIN update_call c
        ON c.id = p.call_id
),
deleted_alloc AS (
    DELETE FROM app.media_room_allocations a
     USING update_call c
     WHERE a.owner_type = 'direct_call'
       AND a.owner_public_id = c.public_id
     RETURNING a.room_id
),
outbox AS (
    INSERT INTO app.realtime_outbox (
        server_node,
        target_user_id,
        target_session_id,
        target_peer_id,
        recipient_user_id,
        recipient_session_id,
        aggregate_type,
        aggregate_id,
        event_type,
        payload,
        status
    )
    SELECT
        us.server_node,
        us.user_id,
        us.id,
        us.peer_id,
        us.user_id,
        us.id,
        'direct_call',
        c.public_id,
        'direct_call_ended',
        jsonb_build_object(
            'type', 'direct_call_ended',
            'object', 'direct_call',
            'callId', c.public_id,
            'roomId', c.media_room_id,
            'actorUserId', $1::uuid::text,
            'actorPeerId', $2,
            'status', c.status
        ),
        'pending'
    FROM update_call c
    JOIN app.user_sessions us
      ON us.peer_id IN (SELECT peer_id FROM participant_sessions)
    RETURNING id
)
SELECT json_build_object(
    'callId', c.public_id,
    'roomId', c.media_room_id,
    'mediaRoomId', (SELECT room_id FROM deleted_alloc LIMIT 1),
    'status', c.status,
    'participantPeerIds', COALESCE((SELECT json_agg(peer_id) FROM participant_sessions), '[]'::json),
    'outboundEvents', COALESCE((
        SELECT json_agg(
            json_build_object(
                'type', 'direct_call_ended',
                'object', 'direct_call',
                'callId', c.public_id,
                'roomId', c.media_room_id,
                'actorUserId', $1::uuid::text,
                'actorPeerId', $2,
                'status', c.status,
                'deliverTo', peer_id
            )
        )
        FROM participant_sessions
    ), '[]'::json)
)
FROM update_call c
LIMIT 1
)SQL";

        return querySingleJson(
            sql,
            {
                std::string(actorUserId),
                std::string(actorPeerId),
                std::string(callPublicId)
            },
            "Direct call ended.");
    }

    std::shared_ptr<MessengerRepository> sharedMessengerRepository() {
        std::lock_guard<std::mutex> lock(gRepositoryMutex);
        return gSharedRepository;
    }

    bool configureSharedMessengerRepository(const std::string& conninfo, std::string& error) {
        error.clear();

        auto client = std::make_shared<PostgresClient>();
        if (!client->connect(conninfo, error)) {
            return false;
        }

        auto repository = std::make_shared<MessengerRepository>(client);
        const auto cleanupStatus = repository->markServerNodeSessionsDisconnected();
        if (!cleanupStatus.ok) {
            error = cleanupStatus.message;
            return false;
        }

        std::lock_guard<std::mutex> lock(gRepositoryMutex);
        gSharedRepository = std::move(repository);
        return true;
    }
    core::contracts::OperationStatus MessengerRepository::handleConferencePeerDisconnected(
        std::string_view peerId,
        std::uintptr_t sessionHandle) {
        if (!isReady()) {
            return dbFailure("Repository is not ready.");
        }
        if (peerId.empty()) {
            return dbFailure("handleConferencePeerDisconnected: peerId must not be empty.");
        }

        static const std::string sql = R"SQL(
WITH peer_session AS (
    SELECT us.id, us.user_id, us.peer_id
      FROM app.user_sessions us
     WHERE us.peer_id = $1
     LIMIT 1
),
affected AS (
    SELECT DISTINCT c.id AS conference_id, c.public_id
      FROM app.conference_sessions cs
      JOIN peer_session ps
        ON ps.id = cs.session_id
      JOIN app.conferences c
        ON c.id = cs.conference_id
     WHERE cs.left_at IS NULL
),
close_session AS (
    UPDATE app.conference_sessions cs
       SET left_at = timezone('utc', now()),
           media_state = 'closed',
           metadata = cs.metadata || jsonb_build_object(
               'disconnectPeerId', $1,
               'disconnectSessionHandle', $2::bigint
           )
      FROM peer_session ps
     WHERE cs.session_id = ps.id
       AND cs.left_at IS NULL
     RETURNING cs.conference_id, cs.user_id
),
update_members AS (
    UPDATE app.conference_members m
       SET membership_status = CASE
               WHEN NOT EXISTS (
                   SELECT 1
                     FROM app.conference_sessions cs2
                    WHERE cs2.conference_id = m.conference_id
                      AND cs2.user_id = m.user_id
                      AND cs2.left_at IS NULL
               ) THEN 'left'
               ELSE m.membership_status
           END,
           left_at = CASE
               WHEN NOT EXISTS (
                   SELECT 1
                     FROM app.conference_sessions cs2
                    WHERE cs2.conference_id = m.conference_id
                      AND cs2.user_id = m.user_id
                      AND cs2.left_at IS NULL
               ) THEN timezone('utc', now())
               ELSE m.left_at
           END,
           updated_at = timezone('utc', now()),
           metadata = m.metadata || jsonb_build_object(
               'disconnectPeerId', $1,
               'disconnectSessionHandle', $2::bigint
           )
      FROM affected a
      JOIN peer_session ps ON TRUE
     WHERE m.conference_id = a.conference_id
       AND m.user_id = ps.user_id
     RETURNING m.conference_id
),
remaining_joined AS (
    SELECT a.conference_id, COUNT(*)::bigint AS cnt
      FROM affected a
      JOIN app.conference_members m
        ON m.conference_id = a.conference_id
       AND m.membership_status = 'joined'
     GROUP BY a.conference_id
),
remaining_peers AS (
    SELECT DISTINCT a.conference_id, us.peer_id
      FROM affected a
      JOIN app.conference_sessions cs
        ON cs.conference_id = a.conference_id
       AND cs.left_at IS NULL
      JOIN app.user_sessions us
        ON us.id = cs.session_id
       AND us.status = 'connected'
),
deleted_alloc AS (
    DELETE FROM app.media_room_allocations ma
     USING affected a
     LEFT JOIN remaining_joined rj
       ON rj.conference_id = a.conference_id
     WHERE ma.owner_type = 'conference'
       AND ma.owner_public_id = a.public_id
       AND COALESCE(rj.cnt, 0) = 0
     RETURNING ma.owner_public_id, ma.room_id
),
deleted_conf AS (
    DELETE FROM app.conferences c
     USING affected a
     LEFT JOIN remaining_joined rj
       ON rj.conference_id = a.conference_id
     WHERE c.id = a.conference_id
       AND COALESCE(rj.cnt, 0) = 0
     RETURNING c.public_id
)
SELECT json_build_object(
    'peerId', $1,
    'conferenceIds', COALESCE(
        (SELECT json_agg(a.public_id) FROM affected a),
        '[]'::json
    ),
    'outboundEvents', COALESCE((
        SELECT json_agg(
            json_build_object(
                'type', 'conference_member_left',
                'object', 'conference',
                'conferencePublicId', a.public_id,
                'userId', ps.user_id::text,
                'peerId', $1,
                'deliverTo', rp.peer_id
            )
        )
        FROM affected a
        JOIN peer_session ps ON TRUE
        JOIN remaining_joined rj
          ON rj.conference_id = a.conference_id
         AND rj.cnt > 0
        JOIN remaining_peers rp
          ON rp.conference_id = a.conference_id
    ), '[]'::json)
)
)SQL";

        return querySingleJson(
            sql,
            {
                std::string(peerId),
                toStringHandle(sessionHandle)
            },
            "Conference disconnect cleanup completed.");
    }

    core::contracts::OperationStatus MessengerRepository::handleDirectCallPeerDisconnected(
        std::string_view peerId,
        std::uintptr_t sessionHandle) {
        if (!isReady()) {
            return dbFailure("Repository is not ready.");
        }
        if (peerId.empty()) {
            return dbFailure("handleDirectCallPeerDisconnected: peerId must not be empty.");
        }

        static const std::string sql = R"SQL(
WITH peer_session AS (
    SELECT us.id, us.user_id, us.peer_id
      FROM app.user_sessions us
     WHERE us.peer_id = $1
     LIMIT 1
),
affected_calls AS (
    SELECT DISTINCT c.id, c.public_id, c.media_room_id
      FROM app.calls c
      JOIN app.call_participants p
        ON p.call_id = c.id
      JOIN peer_session ps
        ON p.joined_session_id = ps.id
        OR p.invited_session_id = ps.id
     WHERE c.status IN ('invited', 'ringing', 'accepted')
),
update_calls AS (
    UPDATE app.calls c
       SET status = 'ended',
           ended_at = COALESCE(c.ended_at, timezone('utc', now()))
      FROM affected_calls a
     WHERE c.id = a.id
     RETURNING c.id, c.public_id, c.media_room_id, c.status
),
update_participants AS (
    UPDATE app.call_participants p
       SET participant_status = 'ended',
           left_at = COALESCE(p.left_at, timezone('utc', now())),
           end_reason = COALESCE(p.end_reason, 'peer_disconnected'),
           updated_at = timezone('utc', now()),
           metadata = p.metadata || jsonb_build_object(
               'disconnectPeerId', $1,
               'disconnectSessionHandle', $2::bigint
           )
      FROM update_calls c
     WHERE p.call_id = c.id
     RETURNING p.call_id, p.user_id
),
event_ins AS (
    INSERT INTO app.call_events (
        call_id,
        actor_user_id,
        actor_session_id,
        event_type,
        payload
    )
    SELECT
        c.id,
        ps.user_id,
        ps.id,
        'ended',
        jsonb_build_object(
            'peerId', $1,
            'reason', 'peer_disconnected'
        )
    FROM update_calls c
    JOIN peer_session ps ON TRUE
    RETURNING id
),
remaining_peers AS (
    SELECT DISTINCT c.id AS call_id, us.peer_id
      FROM update_calls c
      JOIN app.call_participants p
        ON p.call_id = c.id
      JOIN app.user_sessions us
        ON us.user_id = p.user_id
       AND us.status = 'connected'
     WHERE us.peer_id <> $1
),
deleted_alloc AS (
    DELETE FROM app.media_room_allocations ma
     USING update_calls c
     WHERE ma.owner_type = 'direct_call'
       AND ma.owner_public_id = c.public_id
     RETURNING ma.owner_public_id, ma.room_id
)
SELECT json_build_object(
    'peerId', $1,
    'calls', COALESCE((
        SELECT json_agg(
            json_build_object(
                'callId', c.public_id,
                'roomId', c.media_room_id,
                'participantPeerIds', COALESCE((
                    SELECT json_agg(rp.peer_id)
                      FROM remaining_peers rp
                     WHERE rp.call_id = c.id
                ), '[]'::json)
            )
        )
        FROM update_calls c
    ), '[]'::json),
    'outboundEvents', COALESCE((
        SELECT json_agg(
            json_build_object(
                'type', 'direct_call_ended',
                'object', 'direct_call',
                'callId', c.public_id,
                'roomId', c.media_room_id,
                'actorUserId', ps.user_id::text,
                'actorPeerId', $1,
                'status', c.status,
                'reason', 'peer_disconnected',
                'deliverTo', rp.peer_id
            )
        )
        FROM update_calls c
        JOIN peer_session ps ON TRUE
        JOIN remaining_peers rp
          ON rp.call_id = c.id
    ), '[]'::json)
)
)SQL";

        return querySingleJson(
            sql,
            {
                std::string(peerId),
                toStringHandle(sessionHandle)
            },
            "Direct call disconnect cleanup completed.");
    }

} // namespace eds::server_new::infrastructure::db