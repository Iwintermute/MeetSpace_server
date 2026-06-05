#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>
#include <utility>

namespace eds::server_new::protocol {

    using json = nlohmann::json;

    namespace envelope {
        inline constexpr std::string_view kType = "type";
        inline constexpr std::string_view kObject = "object";
        inline constexpr std::string_view kAgent = "agent";
        inline constexpr std::string_view kAction = "action";
        inline constexpr std::string_view kCtx = "ctx";
        inline constexpr std::string_view kOk = "ok";
        inline constexpr std::string_view kMessage = "message";
        inline constexpr std::string_view kCorrelationId = "correlationId";
        inline constexpr std::string_view kClientRequestId = "clientRequestId";
        inline constexpr std::string_view kDeliverTo = "deliverTo";

        inline constexpr std::string_view kPeerAssigned = "peer_assigned";
        inline constexpr std::string_view kDispatchResult = "dispatch_result";
        inline constexpr std::string_view kAudioSessionLifecycle = "audio_session_lifecycle";
    }

    namespace object {
        inline constexpr std::string_view kAuth = "auth";
        inline constexpr std::string_view kConference = "conference";
        inline constexpr std::string_view kConferenceChat = "chat";
        inline constexpr std::string_view kDirectChat = "direct_chat";
        inline constexpr std::string_view kDirectCall = "direct_call";
        inline constexpr std::string_view kMediasoup = "mediasoup";
    }

    namespace agent {
        namespace auth {
            inline constexpr std::string_view kSession = "session";
        }
        namespace conference {
            inline constexpr std::string_view kLifecycle = "lifecycle";
            inline constexpr std::string_view kMembership = "membership";
        }
        namespace chat {
            inline constexpr std::string_view kMessaging = "messaging";
        }
        namespace direct_call {
            inline constexpr std::string_view kLifecycle = "lifecycle";
        }
        namespace direct_chat {
            inline constexpr std::string_view kMessaging = "messaging";
        }
        namespace mediasoup {
            inline constexpr std::string_view kSignaling = "signaling";
        }
    }

    namespace action {
        namespace auth {
            inline constexpr std::string_view kBindSession = "bind_session";
            inline constexpr std::string_view kRestoreSession = "restore_session";
            inline constexpr std::string_view kLogoutSession = "logout_session";
        }

        namespace conference {
            inline constexpr std::string_view kCreateConference = "create_conference";
            inline constexpr std::string_view kGetConference = "get_conference";
            inline constexpr std::string_view kCloseConference = "close_conference";
            inline constexpr std::string_view kJoinConference = "join_conference";
            inline constexpr std::string_view kLeaveConference = "leave_conference";
            inline constexpr std::string_view kListMembers = "list_members";
            inline constexpr std::string_view kPauseTrack = "pause_track";
            inline constexpr std::string_view kResumeTrack = "resume_track";
            inline constexpr std::string_view kCloseTrack = "close_track";
        }

        namespace conference_chat {
            inline constexpr std::string_view kSendMessage = "send_message";
        }

        namespace direct_chat {
            inline constexpr std::string_view kSendMessage = "send_message";
        }

        namespace direct_call {
            inline constexpr std::string_view kCreateCall = "create_call";
            inline constexpr std::string_view kAcceptCall = "accept_call";
            inline constexpr std::string_view kDeclineCall = "decline_call";
            inline constexpr std::string_view kHangupCall = "hangup_call";
            inline constexpr std::string_view kFileOffer = "file_offer";
            inline constexpr std::string_view kFileAccept = "file_accept";
            inline constexpr std::string_view kFileChunk = "file_chunk";
            inline constexpr std::string_view kFileComplete = "file_complete";
            inline constexpr std::string_view kFileCancel = "file_cancel";
            inline constexpr std::string_view kPauseTrack = "pause_track";
            inline constexpr std::string_view kResumeTrack = "resume_track";
            inline constexpr std::string_view kCloseTrack = "close_track";
        }

        namespace mediasoup {
            inline constexpr std::string_view kCreateRoom = "create_room";
            inline constexpr std::string_view kJoinRoom = "join_room";
            inline constexpr std::string_view kLeaveRoom = "leave_room";
            inline constexpr std::string_view kConnectSession = "connect_session";
            inline constexpr std::string_view kDisconnectSession = "disconnect_session";
            inline constexpr std::string_view kOpenTransport = "open_transport";
            inline constexpr std::string_view kProduce = "produce";
            inline constexpr std::string_view kConsume = "consume";
            inline constexpr std::string_view kConsumerReady = "consumer_ready";
            inline constexpr std::string_view kWebrtcOffer = "webrtc_offer";
            inline constexpr std::string_view kWebrtcIce = "webrtc_ice";
            inline constexpr std::string_view kWebrtcClose = "webrtc_close";
            inline constexpr std::string_view kStats = "stats";
        }
    }

    namespace event {
        inline constexpr std::string_view kChatMessage = "chat_message";
        inline constexpr std::string_view kDirectChatMessage = "direct_chat_message";

        inline constexpr std::string_view kDirectCallInvite = "direct_call_invite";
        inline constexpr std::string_view kDirectCallAccepted = "direct_call_accepted";
        inline constexpr std::string_view kDirectCallDeclined = "direct_call_declined";
        inline constexpr std::string_view kDirectCallEnded = "direct_call_ended";
        inline constexpr std::string_view kDirectCallFileOffer = "direct_call_file_offer";
        inline constexpr std::string_view kDirectCallFileAccept = "direct_call_file_accept";
        inline constexpr std::string_view kDirectCallFileChunk = "direct_call_file_chunk";
        inline constexpr std::string_view kDirectCallFileComplete = "direct_call_file_complete";
        inline constexpr std::string_view kDirectCallFileCancel = "direct_call_file_cancel";

        inline constexpr std::string_view kRoomState = "room_state";
        inline constexpr std::string_view kSessionStarted = "session_started";
        inline constexpr std::string_view kSessionEnded = "session_ended";
        inline constexpr std::string_view kPeerJoined = "peer_joined";
        inline constexpr std::string_view kPeerLeft = "peer_left";
        inline constexpr std::string_view kTransportOpened = "transport_opened";
        inline constexpr std::string_view kTrackPublished = "track_published";
        inline constexpr std::string_view kTrackClosed = "track_closed";
        inline constexpr std::string_view kConsumerResumed = "consumer_resumed";
        inline constexpr std::string_view kSessionClosed = "session_closed";
        inline constexpr std::string_view kTransportError = "transport_error";
    }

    struct RequestEnvelope {
        std::string object;
        std::string agent;
        std::string action;
        json ctx = json::object();

        json toJson() const {
            return json{
                { std::string(envelope::kObject), object },
                { std::string(envelope::kAgent), agent },
                { std::string(envelope::kAction), action },
                { std::string(envelope::kCtx), ctx }
            };
        }
    };

    inline RequestEnvelope makeBindSessionRequest(std::string accessToken, std::string deviceId) {
        RequestEnvelope req;
        req.object = std::string(object::kAuth);
        req.agent = std::string(agent::auth::kSession);
        req.action = std::string(action::auth::kBindSession);
        req.ctx = {
            { "accessToken", std::move(accessToken) },
            { "deviceId", std::move(deviceId) }
        };
        return req;
    }

    inline RequestEnvelope makeCreateConferenceRequest(std::string conferenceId, std::string clientRequestId) {
        RequestEnvelope req;
        req.object = std::string(object::kConference);
        req.agent = std::string(agent::conference::kLifecycle);
        req.action = std::string(action::conference::kCreateConference);
        req.ctx = {
            { "conferenceId", std::move(conferenceId) },
            { "clientRequestId", std::move(clientRequestId) }
        };
        return req;
    }

    inline RequestEnvelope makeJoinConferenceRequest(std::string conferenceId, std::string clientRequestId) {
        RequestEnvelope req;
        req.object = std::string(object::kConference);
        req.agent = std::string(agent::conference::kMembership);
        req.action = std::string(action::conference::kJoinConference);
        req.ctx = {
            { "conferenceId", std::move(conferenceId) },
            { "clientRequestId", std::move(clientRequestId) }
        };
        return req;
    }

    inline RequestEnvelope makeLeaveConferenceRequest(std::string conferenceId, std::string clientRequestId) {
        RequestEnvelope req;
        req.object = std::string(object::kConference);
        req.agent = std::string(agent::conference::kMembership);
        req.action = std::string(action::conference::kLeaveConference);
        req.ctx = {
            { "conferenceId", std::move(conferenceId) },
            { "clientRequestId", std::move(clientRequestId) }
        };
        return req;
    }

    inline RequestEnvelope makeConferenceChatMessageRequest(
        std::string conferenceId,
        std::string text,
        std::string clientRequestId,
        std::string targetUserId = {}) {
        RequestEnvelope req;
        req.object = std::string(object::kConferenceChat);
        req.agent = std::string(agent::chat::kMessaging);
        req.action = std::string(action::conference_chat::kSendMessage);
        req.ctx = {
            { "conferenceId", std::move(conferenceId) },
            { "text", std::move(text) },
            { "clientRequestId", std::move(clientRequestId) }
        };
        if (!targetUserId.empty()) {
            req.ctx["targetUserId"] = std::move(targetUserId);
        }
        return req;
    }

    inline RequestEnvelope makeDirectChatMessageRequest(
        std::string targetUserId,
        std::string text,
        std::string clientRequestId) {
        RequestEnvelope req;
        req.object = std::string(object::kDirectChat);
        req.agent = std::string(agent::direct_chat::kMessaging);
        req.action = std::string(action::direct_chat::kSendMessage);
        req.ctx = {
            { "targetUserId", std::move(targetUserId) },
            { "text", std::move(text) },
            { "clientRequestId", std::move(clientRequestId) }
        };
        return req;
    }

    inline RequestEnvelope makeCreateDirectCallRequest(std::string targetUserId, std::string clientRequestId) {
        RequestEnvelope req;
        req.object = std::string(object::kDirectCall);
        req.agent = std::string(agent::direct_call::kLifecycle);
        req.action = std::string(action::direct_call::kCreateCall);
        req.ctx = {
            { "targetUserId", std::move(targetUserId) },
            { "clientRequestId", std::move(clientRequestId) }
        };
        return req;
    }

    inline RequestEnvelope makeAcceptDirectCallRequest(std::string callId, std::string clientRequestId) {
        RequestEnvelope req;
        req.object = std::string(object::kDirectCall);
        req.agent = std::string(agent::direct_call::kLifecycle);
        req.action = std::string(action::direct_call::kAcceptCall);
        req.ctx = {
            { "callId", std::move(callId) },
            { "clientRequestId", std::move(clientRequestId) }
        };
        return req;
    }

    inline RequestEnvelope makeDeclineDirectCallRequest(std::string callId, std::string clientRequestId) {
        RequestEnvelope req;
        req.object = std::string(object::kDirectCall);
        req.agent = std::string(agent::direct_call::kLifecycle);
        req.action = std::string(action::direct_call::kDeclineCall);
        req.ctx = {
            { "callId", std::move(callId) },
            { "clientRequestId", std::move(clientRequestId) }
        };
        return req;
    }

    inline RequestEnvelope makeHangupDirectCallRequest(std::string callId, std::string clientRequestId) {
        RequestEnvelope req;
        req.object = std::string(object::kDirectCall);
        req.agent = std::string(agent::direct_call::kLifecycle);
        req.action = std::string(action::direct_call::kHangupCall);
        req.ctx = {
            { "callId", std::move(callId) },
            { "clientRequestId", std::move(clientRequestId) }
        };
        return req;
    }

    inline RequestEnvelope makeOpenTransportRequest(
        std::string roomId,
        std::string transportId,
        std::string clientRequestId) {
        RequestEnvelope req;
        req.object = std::string(object::kMediasoup);
        req.agent = std::string(agent::mediasoup::kSignaling);
        req.action = std::string(action::mediasoup::kOpenTransport);
        req.ctx = {
            { "roomId", std::move(roomId) },
            { "transportId", std::move(transportId) },
            { "clientRequestId", std::move(clientRequestId) }
        };
        return req;
    }

    inline RequestEnvelope makeWebrtcOfferRequest(
        std::string roomId,
        std::string transportId,
        json dtlsParameters,
        std::string clientRequestId) {
        RequestEnvelope req;
        req.object = std::string(object::kMediasoup);
        req.agent = std::string(agent::mediasoup::kSignaling);
        req.action = std::string(action::mediasoup::kWebrtcOffer);
        req.ctx = {
            { "roomId", std::move(roomId) },
            { "transportId", std::move(transportId) },
            { "dtlsParameters", std::move(dtlsParameters) },
            { "clientRequestId", std::move(clientRequestId) }
        };
        return req;
    }

    inline RequestEnvelope makeWebrtcIceRequest(
        std::string roomId,
        std::string transportId,
        json candidate,
        std::string clientRequestId) {
        RequestEnvelope req;
        req.object = std::string(object::kMediasoup);
        req.agent = std::string(agent::mediasoup::kSignaling);
        req.action = std::string(action::mediasoup::kWebrtcIce);
        req.ctx = {
            { "roomId", std::move(roomId) },
            { "transportId", std::move(transportId) },
            { "candidate", candidate.dump() },
            { "clientRequestId", std::move(clientRequestId) }
        };
        return req;
    }

    inline RequestEnvelope makeProduceRequest(
        std::string roomId,
        std::string transportId,
        std::string producerId,
        std::string kind,
        json rtpParameters,
        std::string clientRequestId) {
        RequestEnvelope req;
        req.object = std::string(object::kMediasoup);
        req.agent = std::string(agent::mediasoup::kSignaling);
        req.action = std::string(action::mediasoup::kProduce);
        req.ctx = {
            { "roomId", std::move(roomId) },
            { "transportId", std::move(transportId) },
            { "producerId", std::move(producerId) },
            { "kind", std::move(kind) },
            { "rtpParameters", std::move(rtpParameters) },
            { "clientRequestId", std::move(clientRequestId) }
        };
        return req;
    }

    inline RequestEnvelope makeConsumeRequest(
        std::string roomId,
        std::string transportId,
        std::string producerId,
        std::string consumerId,
        json rtpCapabilities,
        std::string clientRequestId) {
        RequestEnvelope req;
        req.object = std::string(object::kMediasoup);
        req.agent = std::string(agent::mediasoup::kSignaling);
        req.action = std::string(action::mediasoup::kConsume);
        req.ctx = {
            { "roomId", std::move(roomId) },
            { "transportId", std::move(transportId) },
            { "producerId", std::move(producerId) },
            { "consumerId", std::move(consumerId) },
            { "rtpCapabilities", std::move(rtpCapabilities) },
            { "clientRequestId", std::move(clientRequestId) }
        };
        return req;
    }
   

} // namespace eds::server_new::protocol