#include "includes.h"
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <string>
#include <optional>
#include <vector>

using json = nlohmann::json;

struct ClientSession {
    std::string peerId;
    std::string authToken;
    int conferenceId{-1};
};

struct Conference {
    int id{-1};
    std::string title;
    std::string password;
    std::unordered_set<void*> members;
};

int main() {
    using namespace Sys::Network;
    using namespace Sys::Rtc;
    using namespace Sys::Media;

    // IoContext
    cNetIoContext ioCtx;
    ioCtx.fnInit();
    ioCtx.fnStart();

    //  HTTP сервер
    cNetHttpServer httpServer(ioCtx.fnIo(), 8080);
    httpServer.fnSetHealthFn([]() { return R"({"status":"ok"})"; });
    httpServer.fnStart();

    // Shared state for WS signaling
    std::mutex stateMutex;
    std::unordered_map<void*, ClientSession> clients;
    std::unordered_map<int, Conference> conferences;
    int nextConferenceId = 1;

    auto sendError = [&](void* session, const std::string& message) {
        json payload;
        payload["type"] = "error";
        payload["message"] = message;
        return payload.dump();
    };

    auto serializeAndSend = [](cNetWebSocketServer& server, void* session, const json& payload) {
        server.fnSendText(session, payload.dump());
    };

    auto broadcastConference = [&](cNetWebSocketServer& server, int conferenceId, const json& payload, void* skip = nullptr) {
        std::vector<void*> targets;
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            auto confIt = conferences.find(conferenceId);
            if (confIt == conferences.end()) return;
            for (auto* member : confIt->second.members) {
                if (member != skip) {
                    targets.push_back(member);
                }
            }
        }
        for (auto* target : targets) {
            server.fnSendText(target, payload.dump());
        }
    };

    auto leaveConference = [&](cNetWebSocketServer& server, void* session) {
        std::optional<int> leftConference;
        std::string peerId;
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            auto it = clients.find(session);
            if (it == clients.end()) return;

            if (it->second.conferenceId == -1) return;

            leftConference = it->second.conferenceId;
            peerId = it->second.peerId;
            auto confIt = conferences.find(*leftConference);
            if (confIt != conferences.end()) {
                confIt->second.members.erase(session);
                if (confIt->second.members.empty()) {
                    conferences.erase(confIt);
                }
            }
            it->second.conferenceId = -1;
        }

        if (leftConference.has_value()) {
            json leftPayload;
            leftPayload["type"] = "peer_left";
            leftPayload["peer_id"] = peerId;
            broadcastConference(server, *leftConference, leftPayload, session);
        }
    };

    // WebSocket сервер
    cNetWebSocketServer wsServer(ioCtx.fnIo(), 9000);
    wsServer.fnSetOnConnected([&](void* session) {
        std::lock_guard<std::mutex> lock(stateMutex);
        clients.emplace(session, ClientSession{});
        std::cout << "[WS] client connected: " << session << std::endl;
        });
    wsServer.fnSetOnDisconnected([&](void* session) {
        leaveConference(wsServer, session);
        std::lock_guard<std::mutex> lock(stateMutex);
        clients.erase(session);
        std::cout << "[WS] client disconnected: " << session << std::endl;
        });
    wsServer.fnSetOnMessage([&](const std::string& msg, void* session) {
        json incoming;
        try {
            incoming = json::parse(msg);
        }
        catch (const std::exception& e) {
            wsServer.fnSendText(session, sendError(session, std::string("Invalid JSON: ") + e.what()));
            return;
        }

        const std::string type = incoming.value("type", "");
        std::string peerId;
        std::string authToken;
        int currentConference = -1;
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            auto it = clients.find(session);
            if (it != clients.end()) {
                peerId = it->second.peerId;
                authToken = it->second.authToken;
                currentConference = it->second.conferenceId;
            }
        }

        auto ensureAuthorized = [&]() -> bool {
            if (authToken.empty()) {
                wsServer.fnSendText(session, sendError(session, "Unauthorized: missing auth token"));
                return false;
            }
            return true;
        };

        if (type == "register") {
            std::lock_guard<std::mutex> lock(stateMutex);
            auto it = clients.find(session);
            if (it != clients.end()) {
                it->second.peerId = incoming.value("peer_id", peerId);
                it->second.authToken = incoming.value("auth_token", authToken);
                peerId = it->second.peerId;
                authToken = it->second.authToken;
            }

            json welcome;
            welcome["type"] = "welcome";
            welcome["peer_id"] = peerId;
            serializeAndSend(wsServer, session, welcome);
            return;
        }

        if (type == "create_conference") {
            if (!ensureAuthorized()) return;

            Conference conf;
            conf.id = nextConferenceId++;
            conf.title = incoming.value("title", "Untitled");
            conf.password = incoming.value("password", "");
            conf.members.insert(session);

            {
                std::lock_guard<std::mutex> lock(stateMutex);
                clients[session].conferenceId = conf.id;
                conferences.emplace(conf.id, std::move(conf));
            }

            json created;
            created["type"] = "conference_created";
            created["conference_id"] = clients[session].conferenceId;
            serializeAndSend(wsServer, session, created);

            json joined;
            joined["type"] = "conference_joined";
            joined["conference_id"] = clients[session].conferenceId;
            joined["peer_id"] = peerId;
            serializeAndSend(wsServer, session, joined);
            return;
        }

        if (type == "join_conference") {
            if (!ensureAuthorized()) return;

            int requestedId = incoming.value("conference_id", -1);
            std::string password = incoming.value("password", "");

            std::string joinPeerId = peerId;
            {
                std::lock_guard<std::mutex> lock(stateMutex);
                auto confIt = conferences.find(requestedId);
                if (confIt == conferences.end()) {
                    wsServer.fnSendText(session, sendError(session, "Conference not found"));
                    return;
                }
                if (!confIt->second.password.empty() && confIt->second.password != password) {
                    wsServer.fnSendText(session, sendError(session, "Invalid conference token"));
                    return;
                }
            }

            leaveConference(wsServer, session);

            bool joined = false;
            {
                std::lock_guard<std::mutex> lock(stateMutex);
                auto confIt = conferences.find(requestedId);
                if (confIt == conferences.end()) {
                    wsServer.fnSendText(session, sendError(session, "Conference closed"));
                    return;
                }
                confIt->second.members.insert(session);
                clients[session].conferenceId = requestedId;
                joinPeerId = clients[session].peerId;
                joined = true;
            }

            if (joined) {
                json joinedMsg;
                joinedMsg["type"] = "conference_joined";
                joinedMsg["conference_id"] = requestedId;
                joinedMsg["peer_id"] = joinPeerId;
                serializeAndSend(wsServer, session, joinedMsg);

                json peerJoined;
                peerJoined["type"] = "peer_joined";
                peerJoined["peer_id"] = joinPeerId;
                broadcastConference(wsServer, requestedId, peerJoined, session);
            }
            return;
        }

        if (type == "leave_conference") {
            leaveConference(wsServer, session);
            return;
        }

        if (type == "chat_message") {
            if (currentConference == -1) {
                wsServer.fnSendText(session, sendError(session, "Join a conference first"));
                return;
            }

            json chatPayload = incoming;
            chatPayload["peer_id"] = peerId;
            broadcastConference(wsServer, currentConference, chatPayload, nullptr);
            return;
        }

        if (type == "audio_frame") {
            if (currentConference == -1) {
                return;
            }
            json audioPayload = incoming;
            audioPayload["peer_id"] = peerId;
            broadcastConference(wsServer, currentConference, audioPayload, session);
            return;
        }

        if (type == "reaction") {
            if (currentConference == -1) {
                return;
            }
            json reactionPayload = incoming;
            reactionPayload["peer_id"] = peerId;
            broadcastConference(wsServer, currentConference, reactionPayload, session);
            return;
        }

        wsServer.fnSendText(session, sendError(session, "Unknown message type"));
        });
    wsServer.fnSetOnBinary([&wsServer](const std::vector<uint8_t>& data, void* session) {
        // Legacy binary forwarding: broadcast raw frames to other peers
        wsServer.fnBroadcastBinary(data, session);
        });
    wsServer.fnStart();

    // RTC Manager
    cRtcManager rtcMgr([&wsServer](void* session, const std::string& sMsg) {
        // Отправляем клиенту через WS
        // std::cout << "[RTC->WS] " << sMsg << std::endl;
        });
    rtcMgr.fnInit();

    std::cout << "Server running... Press Enter to quit.\n";
    std::cin.get();

    ioCtx.fnStop();
    httpServer.fnStop();
    wsServer.fnStop();

    return 0;
}
