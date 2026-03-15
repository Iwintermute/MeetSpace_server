#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif
#include <nlohmann/json.hpp>

#include "AudioStreamer.h"

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;

namespace {

std::atomic<bool> g_running{ true };
std::unique_ptr<AudioStreamer> g_audioStreamer;
std::mutex g_peerMutex;
std::string g_assignedPeer;
std::atomic<std::uint64_t> g_clientRequestCounter{ 0 };
std::atomic<std::uint64_t> g_chatSendAttemptCounter{ 0 };
std::atomic<std::uint64_t> g_chatSendAcceptedCounter{ 0 };
std::atomic<std::uint64_t> g_chatSendRejectedCounter{ 0 };
std::atomic<std::uint64_t> g_chatReceivedCounter{ 0 };
std::atomic<std::uint64_t> g_chatBroadcastCounter{ 0 };
std::atomic<std::uint64_t> g_chatDirectCounter{ 0 };
std::atomic<std::uint64_t> g_audioLifecycleStartedCounter{ 0 };
std::atomic<std::uint64_t> g_audioLifecycleEndedCounter{ 0 };
std::atomic<std::uint64_t> g_audioLifecycleUnknownCounter{ 0 };
std::mutex g_chatContextMutex;
std::string g_activeConferenceId;
std::vector<std::string> g_cachedConferencePeers;

constexpr const char* kDefaultSdpOffer =
    "v=0\r\n"
    "o=- 0 0 IN IP4 127.0.0.1\r\n"
    "s=cli\r\n"
    "t=0 0\r\n"
    "a=group:BUNDLE 0\r\n"
    "m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=mid:0\r\n"
    "a=sctp-port:5000\r\n";

namespace console {
std::mutex g_consoleMutex;

std::string buildTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif

    std::ostringstream oss;
    oss << "[" << std::put_time(&tm, "%H:%M:%S") << "." << std::setfill('0') << std::setw(3) << ms.count() << "] ";
    return oss.str();
}

void writeLine(const std::string& message, int color = 7, bool error = false, bool withTimestamp = true) {
    std::lock_guard<std::mutex> lock(g_consoleMutex);
#ifdef _WIN32
    const HANDLE hConsole = GetStdHandle(error ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, color);
#endif
    std::ostream& stream = error ? std::cerr : std::cout;
    if (withTimestamp) {
        stream << buildTimestamp();
    }
    stream << message << "\n";
#ifdef _WIN32
    SetConsoleTextAttribute(hConsole, 7);
#endif
}
} // namespace console

void setAssignedPeer(const std::string& peer) {
    std::lock_guard<std::mutex> lock(g_peerMutex);
    g_assignedPeer = peer;
}

std::string getAssignedPeer() {
    std::lock_guard<std::mutex> lock(g_peerMutex);
    return g_assignedPeer;
}

void setActiveConference(std::string conferenceId, bool verbose = true) {
    if (conferenceId.empty()) {
        return;
    }
    const auto conferenceToPrint = conferenceId;

    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(g_chatContextMutex);
        if (g_activeConferenceId != conferenceId) {
            g_activeConferenceId = std::move(conferenceId);
            g_cachedConferencePeers.clear();
            changed = true;
        }
    }

    if (changed && verbose) {
        console::writeLine("[chat.context] active conference=" + conferenceToPrint, 10);
    }
}

std::string getActiveConference() {
    std::lock_guard<std::mutex> lock(g_chatContextMutex);
    return g_activeConferenceId;
}

void setKnownConferencePeers(std::vector<std::string> peers) {
    peers.erase(
        std::remove_if(peers.begin(), peers.end(), [](const auto& item) { return item.empty(); }),
        peers.end());
    std::sort(peers.begin(), peers.end());
    peers.erase(std::unique(peers.begin(), peers.end()), peers.end());

    std::lock_guard<std::mutex> lock(g_chatContextMutex);
    g_cachedConferencePeers = std::move(peers);
}

void rememberKnownConferencePeer(const std::string& peerId) {
    if (peerId.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_chatContextMutex);
    if (std::find(g_cachedConferencePeers.begin(), g_cachedConferencePeers.end(), peerId) == g_cachedConferencePeers.end()) {
        g_cachedConferencePeers.push_back(peerId);
    }
}

std::vector<std::string> getKnownConferencePeers() {
    std::lock_guard<std::mutex> lock(g_chatContextMutex);
    return g_cachedConferencePeers;
}

void printKnownConferencePeers() {
    const auto peers = getKnownConferencePeers();
    if (peers.empty()) {
        console::writeLine("[chat.peers] cache is empty. Use chat.members to refresh.", 14);
        return;
    }

    std::ostringstream line;
    line << "[chat.peers] ";
    for (std::size_t index = 0; index < peers.size(); ++index) {
        if (index > 0) {
            line << ", ";
        }
        line << peers[index];
    }
    console::writeLine(line.str(), 11);
}

void printChatContextStatus() {
    const auto conferenceId = getActiveConference();
    if (conferenceId.empty()) {
        console::writeLine("[chat.context] active conference is not set", 14);
    }
    else {
        console::writeLine("[chat.context] active conference=" + conferenceId, 11);
    }
    printKnownConferencePeers();
}

std::string nextClientRequestId(std::string_view prefix) {
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const auto sequence = g_clientRequestCounter.fetch_add(1) + 1;
    return std::string(prefix) + "-" + std::to_string(now) + "-" + std::to_string(sequence);
}

nlohmann::json makeMediasoupRequest(const std::string& action, nlohmann::json context = nlohmann::json::object()) {
    const auto assignedPeer = getAssignedPeer();
    if (!assignedPeer.empty() && !context.contains("peer")) {
        context["peer"] = assignedPeer;
    }

    return nlohmann::json{
        {"object", "mediasoup"},
        {"agent", "signaling"},
        {"action", action},
        {"ctx", std::move(context)}
    };
}

std::string resolveConferenceAgent(const std::string& action) {
    if (action == "create_conference" || action == "get_conference" || action == "close_conference") {
        return "lifecycle";
    }
    return "membership";
}

nlohmann::json makeConferenceRequest(const std::string& action, nlohmann::json context = nlohmann::json::object()) {
    const auto assignedPeer = getAssignedPeer();
    if (!assignedPeer.empty() && !context.contains("peerId")) {
        context["peerId"] = assignedPeer;
    }
    if (!context.contains("clientRequestId")) {
        context["clientRequestId"] = nextClientRequestId("conference");
    }

    return nlohmann::json{
        {"object", "conference"},
        {"agent", resolveConferenceAgent(action)},
        {"action", action},
        {"ctx", std::move(context)}
    };
}

nlohmann::json makeChatRequest(const std::string& action, nlohmann::json context = nlohmann::json::object()) {
    const auto assignedPeer = getAssignedPeer();
    if (!assignedPeer.empty() && !context.contains("peerId")) {
        context["peerId"] = assignedPeer;
    }
    if (!context.contains("clientRequestId")) {
        context["clientRequestId"] = nextClientRequestId("chat");
    }

    return nlohmann::json{
        {"object", "chat"},
        {"agent", "messaging"},
        {"action", action},
        {"ctx", std::move(context)}
    };
}

void fail(beast::error_code ec, const char* what) {
    console::writeLine(std::string("Error: ") + what + ": " + ec.message(), 12, true);
}

bool sendText(websocket::stream<beast::tcp_stream>& ws, const std::string& text) {
    try {
        ws.write(net::buffer(text));
        console::writeLine("> " + text, 11);
        return true;
    }
    catch (const std::exception& e) {
        console::writeLine(std::string("write error: ") + e.what(), 12, true);
        g_running = false;
        return false;
    }
}

bool sendJson(websocket::stream<beast::tcp_stream>& ws, const nlohmann::json& payload) {
    return sendText(ws, payload.dump());
}

bool sendMediasoupAction(websocket::stream<beast::tcp_stream>& ws, const std::string& action, nlohmann::json context = nlohmann::json::object()) {
    return sendJson(ws, makeMediasoupRequest(action, std::move(context)));
}

bool sendConferenceAction(websocket::stream<beast::tcp_stream>& ws, const std::string& action, nlohmann::json context = nlohmann::json::object()) {
    return sendJson(ws, makeConferenceRequest(action, std::move(context)));
}

bool sendChatAction(websocket::stream<beast::tcp_stream>& ws, const std::string& action, nlohmann::json context = nlohmann::json::object()) {
    if (action == "send_message") {
        g_chatSendAttemptCounter.fetch_add(1);
    }
    return sendJson(ws, makeChatRequest(action, std::move(context)));
}

void sleepStep() {
    std::this_thread::sleep_for(std::chrono::milliseconds(140));
}

bool containsAnyToken(const std::string& text, std::string_view tokenA, std::string_view tokenB) {
    return text.find(tokenA) != std::string::npos || text.find(tokenB) != std::string::npos;
}

void accountAudioLifecycleFromRawMessage(const std::string& message) {
    const bool hasLifecycleType = containsAnyToken(
        message,
        "\"type\":\"audio_session_lifecycle\"",
        "\"type\": \"audio_session_lifecycle\"");
    if (!hasLifecycleType) {
        return;
    }

    const bool started = containsAnyToken(message, "\"started\":true", "\"started\": true");
    const bool ended = containsAnyToken(message, "\"ended\":true", "\"ended\": true");
    if (started) {
        g_audioLifecycleStartedCounter.fetch_add(1);
    }
    if (ended) {
        g_audioLifecycleEndedCounter.fetch_add(1);
    }
    if (!started && !ended) {
        g_audioLifecycleUnknownCounter.fetch_add(1);
    }
}

bool updatePeersFromConferenceMembersPayload(const nlohmann::json& payload) {
    if (!payload.is_object()) {
        return false;
    }

    const auto conferenceId = payload.value("conferenceId", std::string{});
    if (!conferenceId.empty()) {
        setActiveConference(conferenceId, false);
    }

    if (!payload.contains("members") || !payload["members"].is_array()) {
        return false;
    }

    std::vector<std::string> peers;
    for (const auto& item : payload["members"]) {
        if (item.is_object()) {
            const auto peerId = item.value("peerId", std::string{});
            if (!peerId.empty()) {
                peers.push_back(peerId);
            }
        }
        else if (item.is_string()) {
            peers.push_back(item.get<std::string>());
        }
    }

    setKnownConferencePeers(std::move(peers));
    return true;
}

bool tryUpdatePeersFromDispatchResult(const nlohmann::json& dispatchResult) {
    if (!dispatchResult.is_object()) {
        return false;
    }
    if (dispatchResult.value("type", std::string{}) != "dispatch_result") {
        return false;
    }
    if (dispatchResult.value("object", std::string{}) != "conference") {
        return false;
    }
    if (dispatchResult.value("action", std::string{}) != "list_members") {
        return false;
    }
    if (!dispatchResult.value("ok", false)) {
        return false;
    }
    if (!dispatchResult.contains("message")) {
        return false;
    }

    try {
        nlohmann::json payload = nlohmann::json::object();
        if (dispatchResult["message"].is_string()) {
            payload = nlohmann::json::parse(dispatchResult["message"].get<std::string>());
        }
        else if (dispatchResult["message"].is_object()) {
            payload = dispatchResult["message"];
        }
        else {
            return false;
        }

        if (updatePeersFromConferenceMembersPayload(payload)) {
            printKnownConferencePeers();
            return true;
        }
    }
    catch (...) {
    }

    return false;
}

bool requestConferenceMembers(
    websocket::stream<beast::tcp_stream>& ws,
    const std::string& conferenceId) {
    if (conferenceId.empty()) {
        return false;
    }
    return sendConferenceAction(ws, "list_members", nlohmann::json{ {"conferenceId", conferenceId} });
}

bool sendMessageToActiveConference(
    websocket::stream<beast::tcp_stream>& ws,
    const std::string& text,
    const std::string& targetPeerId = std::string{}) {
    const auto conferenceId = getActiveConference();
    if (conferenceId.empty()) {
        console::writeLine("[chat.context] active conference is not set. Use chat.join/chat.create first.", 14);
        return true;
    }
    if (text.empty()) {
        return true;
    }

    nlohmann::json payload{
        {"conferenceId", conferenceId},
        {"text", text}
    };
    if (!targetPeerId.empty()) {
        payload["targetPeerId"] = targetPeerId;
        rememberKnownConferencePeer(targetPeerId);
    }

    return sendChatAction(ws, "send_message", std::move(payload));
}

bool runMediasoupFullCycle(websocket::stream<beast::tcp_stream>& ws, std::string roomId) {
    if (roomId.empty()) {
        roomId = nextClientRequestId("room");
    }

    console::writeLine("[test.mediasoup] start roomId=" + roomId, 10);
    if (!sendMediasoupAction(ws, "create_room", nlohmann::json{ {"roomId", roomId} })) return false;
    sleepStep();
    if (!sendMediasoupAction(ws, "join_room", nlohmann::json{ {"roomId", roomId} })) return false;
    sleepStep();
    if (!sendMediasoupAction(ws, "open_transport", nlohmann::json{
        {"roomId", roomId},
        {"transportId", "transport-cli-01"}
    })) return false;
    sleepStep();
    if (!sendMediasoupAction(ws, "produce", nlohmann::json{
        {"roomId", roomId},
        {"transportId", "transport-cli-01"},
        {"producerId", "producer-audio-cli-01"},
        {"kind", "audio"}
    })) return false;
    sleepStep();
    if (!sendMediasoupAction(ws, "webrtc_offer", nlohmann::json{ {"sdp", std::string(kDefaultSdpOffer)} })) return false;
    sleepStep();
    if (!sendMediasoupAction(ws, "webrtc_ice", nlohmann::json{
        {"sdpMid", "0"},
        {"candidate", "candidate:0 1 UDP 2122252543 127.0.0.1 5000 typ host"}
    })) return false;
    sleepStep();
    if (!sendMediasoupAction(ws, "webrtc_close")) return false;

    console::writeLine("[test.mediasoup] scenario queued", 10);
    return true;
}

bool runConferenceCycle(websocket::stream<beast::tcp_stream>& ws, std::string conferenceId) {
    if (conferenceId.empty()) {
        conferenceId = nextClientRequestId("conf");
    }

    console::writeLine("[test.conference] start conferenceId=" + conferenceId, 10);
    if (!sendConferenceAction(ws, "create_conference", nlohmann::json{ {"conferenceId", conferenceId} })) return false;
    sleepStep();
    if (!sendConferenceAction(ws, "get_conference", nlohmann::json{ {"conferenceId", conferenceId} })) return false;
    sleepStep();
    if (!sendConferenceAction(ws, "join_conference", nlohmann::json{ {"conferenceId", conferenceId} })) return false;
    sleepStep();
    if (!sendConferenceAction(ws, "list_members", nlohmann::json{ {"conferenceId", conferenceId} })) return false;
    sleepStep();
    if (!sendConferenceAction(ws, "leave_conference", nlohmann::json{ {"conferenceId", conferenceId} })) return false;
    sleepStep();
    if (!sendConferenceAction(ws, "close_conference", nlohmann::json{ {"conferenceId", conferenceId} })) return false;
    console::writeLine("[test.conference] scenario queued", 10);
    return true;
}

bool runChatCycle(websocket::stream<beast::tcp_stream>& ws, std::string conferenceId, std::string text) {
    if (conferenceId.empty()) {
        conferenceId = nextClientRequestId("conf-chat");
    }
    if (text.empty()) {
        text = "chat-test-broadcast";
    }

    console::writeLine("[test.chat] start conferenceId=" + conferenceId, 10);
    if (!sendConferenceAction(ws, "create_conference", nlohmann::json{ {"conferenceId", conferenceId} })) return false;
    sleepStep();
    if (!sendConferenceAction(ws, "join_conference", nlohmann::json{ {"conferenceId", conferenceId} })) return false;
    sleepStep();
    if (!sendConferenceAction(ws, "list_members", nlohmann::json{ {"conferenceId", conferenceId} })) return false;
    sleepStep();
    if (!sendChatAction(ws, "send_message", nlohmann::json{
        {"conferenceId", conferenceId},
        {"text", text}
    })) return false;
    sleepStep();
    if (!sendChatAction(ws, "send_message", nlohmann::json{
        {"conferenceId", conferenceId},
        {"targetPeerId", "ghost-peer"},
        {"text", "chat-test-invalid-target"}
    })) return false;

    console::writeLine("[test.chat] scenario queued. Для проверки получения/бродкаста запустите второй CLI и выполните chat.send в ту же конференцию.", 14);
    return true;
}
bool runAudioCheckCycle(websocket::stream<beast::tcp_stream>& ws, std::string roomId) {
    if (roomId.empty()) {
        roomId = nextClientRequestId("room-audio");
    }
    const auto transportId = nextClientRequestId("transport-audio");
    const auto producerId = nextClientRequestId("producer-audio");

    console::writeLine("[test.audio] start roomId=" + roomId, 10);
    if (!sendMediasoupAction(ws, "create_room", nlohmann::json{ {"roomId", roomId} })) return false;
    sleepStep();
    if (!sendMediasoupAction(ws, "join_room", nlohmann::json{ {"roomId", roomId} })) return false;
    sleepStep();
    if (!sendMediasoupAction(ws, "open_transport", nlohmann::json{
        {"roomId", roomId},
        {"transportId", transportId}
    })) return false;
    sleepStep();
    if (!sendMediasoupAction(ws, "produce", nlohmann::json{
        {"roomId", roomId},
        {"transportId", transportId},
        {"producerId", producerId},
        {"kind", "audio"}
    })) return false;
    sleepStep();
    if (!sendMediasoupAction(ws, "leave_room", nlohmann::json{ {"roomId", roomId} })) return false;

    console::writeLine("[test.audio] scenario queued. Проверьте test.audio.stats для событий audio_session_lifecycle.", 10);
    return true;
}

bool runFullScenario(
    websocket::stream<beast::tcp_stream>& ws,
    std::string roomId,
    std::string conferenceId,
    std::string text) {
    console::writeLine("[test.full] start mediasoup + conference + chat", 10);
    if (!runMediasoupFullCycle(ws, std::move(roomId))) {
        return false;
    }
    sleepStep();
    if (!runConferenceCycle(ws, std::move(conferenceId))) {
        return false;
    }
    sleepStep();
    if (!runChatCycle(ws, {}, std::move(text))) {
        return false;
    }
    console::writeLine("[test.full] scenario queued", 10);
    return true;
}
void resetChatStats() {
    g_chatSendAttemptCounter.store(0);
    g_chatSendAcceptedCounter.store(0);
    g_chatSendRejectedCounter.store(0);
    g_chatReceivedCounter.store(0);
    g_chatBroadcastCounter.store(0);
    g_chatDirectCounter.store(0);
}

void printChatStats() {
    std::ostringstream stats;
    stats << "[test.chat.stats] sent=" << g_chatSendAttemptCounter.load()
          << " accepted=" << g_chatSendAcceptedCounter.load()
          << " rejected=" << g_chatSendRejectedCounter.load()
          << " received=" << g_chatReceivedCounter.load()
          << " broadcast=" << g_chatBroadcastCounter.load()
          << " direct=" << g_chatDirectCounter.load();
    console::writeLine(stats.str(), 11);
}

void resetAudioLifecycleStats() {
    g_audioLifecycleStartedCounter.store(0);
    g_audioLifecycleEndedCounter.store(0);
    g_audioLifecycleUnknownCounter.store(0);
}

void printAudioLifecycleStats() {
    std::ostringstream stats;
    stats << "[test.audio.stats] started=" << g_audioLifecycleStartedCounter.load()
          << " ended=" << g_audioLifecycleEndedCounter.load()
          << " unknown=" << g_audioLifecycleUnknownCounter.load();
    console::writeLine(stats.str(), 11);
}

void printAudioStats() {
    if (!g_audioStreamer) {
        console::writeLine("[audio] streamer not initialized", 14);
        return;
    }

    uint32_t sent = 0;
    uint32_t received = 0;
    uint32_t lost = 0;
    uint64_t bytesSent = 0;
    uint64_t bytesReceived = 0;
    float jitter = 0.0f;
    g_audioStreamer->getStats(sent, received, lost, bytesSent, bytesReceived, jitter);

    std::ostringstream oss;
    oss << "[audio.stats] state=" << g_audioStreamer->getStateString()
        << " sent=" << sent << " recv=" << received
        << " lost=" << lost
        << " bytesSentKB=" << (bytesSent / 1024)
        << " bytesRecvKB=" << (bytesReceived / 1024)
        << " jitterMs=" << std::fixed << std::setprecision(2) << jitter;
    console::writeLine(oss.str(), 11);
}

void showAudioDevices() {
    console::writeLine("=== Input Devices ===", 11, false, false);
    const auto inputs = AudioStreamer::getInputDevices();
    for (const auto& dev : inputs) {
        console::writeLine("  " + dev.first + ": " + dev.second, 7, false, false);
    }

    console::writeLine("=== Output Devices ===", 11, false, false);
    const auto outputs = AudioStreamer::getOutputDevices();
    for (const auto& dev : outputs) {
        console::writeLine("  " + dev.first + ": " + dev.second, 7, false, false);
    }
}

void printHelp(bool audioEnabled) {
    console::writeLine("Команды:", 11, false, false);
    console::writeLine("  help | ?                                      - справка", 7, false, false);
    console::writeLine("  quit | exit | q                               - завершить CLI", 7, false, false);
    console::writeLine("  send <json>                                   - отправить сырой JSON", 7, false, false);
    console::writeLine("", 7, false, false);
    console::writeLine("Автотесты:", 11, false, false);
    console::writeLine("  test.mediasoup [roomId]                       - полный цикл подключения mediasoup", 7, false, false);
    console::writeLine("  test.conference [conferenceId]                - create/get/join/list/leave/close", 7, false, false);
    console::writeLine("  test.chat [conferenceId] [text]               - отправка + проверка бродкаста/ошибки target", 7, false, false);
    console::writeLine("  test.full [roomId] [conferenceId] [text]      - единый сценарий mediasoup+conference+chat", 7, false, false);
    console::writeLine("  test.chat.stats [reset] | chat.stats [reset]  - счётчики отправки/получения chat", 7, false, false);
    console::writeLine("  test.audio [roomId]                           - проверка audio lifecycle (direct mediasoup test mode)", 7, false, false);
    console::writeLine("  test.audio.stats [reset]                      - счётчики audio_session_lifecycle", 7, false, false);
    console::writeLine("", 7, false, false);
    console::writeLine("Ручные тесты mediasoup:", 11, false, false);
    console::writeLine("  direct ms.* доступен только для тестов при --allow-direct-mediasoup на сервере", 14, false, false);
    console::writeLine("  ms.create [roomId]", 7, false, false);
    console::writeLine("  ms.join <roomId>", 7, false, false);
    console::writeLine("  ms.transport <roomId> [transportId]", 7, false, false);
    console::writeLine("  ms.produce <roomId> [transportId] [producerId] [kind]", 7, false, false);
    console::writeLine("  ms.offer [sdp]", 7, false, false);
    console::writeLine("  ms.ice [sdpMid] [candidate]", 7, false, false);
    console::writeLine("  ms.close", 7, false, false);
    console::writeLine("", 7, false, false);
    console::writeLine("Ручные тесты conference:", 11, false, false);
    console::writeLine("  conf.create [conferenceId]", 7, false, false);
    console::writeLine("  conf.get <conferenceId>", 7, false, false);
    console::writeLine("  conf.join <conferenceId>", 7, false, false);
    console::writeLine("  conf.leave <conferenceId>", 7, false, false);
    console::writeLine("  conf.close <conferenceId>", 7, false, false);
    console::writeLine("  conf.members <conferenceId>", 7, false, false);
    console::writeLine("", 7, false, false);
    console::writeLine("Ручные тесты chat:", 11, false, false);
    console::writeLine("  chat.send <conferenceId> <text>", 7, false, false);
    console::writeLine("  chat.to <conferenceId> <peerId> <text>", 7, false, false);
    console::writeLine("  chat.create [conferenceId]                      - создать и выбрать активную конференцию", 7, false, false);
    console::writeLine("  chat.join <conferenceId>                        - войти и выбрать активную конференцию", 7, false, false);
    console::writeLine("  chat.use <conferenceId>                         - вручную выбрать активную конференцию", 7, false, false);
    console::writeLine("  chat.members [conferenceId]                     - обновить кэш участников", 7, false, false);
    console::writeLine("  chat.peers                                      - показать кэш известных peerId", 7, false, false);
    console::writeLine("  chat.active                                     - показать активную конференцию", 7, false, false);
    console::writeLine("  msg <text>                                      - broadcast в активную конференцию", 7, false, false);
    console::writeLine("  msg.to <peerId> <text>                          - direct message в активной конференции", 7, false, false);
    console::writeLine("", 7, false, false);

    if (audioEnabled) {
        console::writeLine("Audio:", 11, false, false);
        console::writeLine("  audio start", 7, false, false);
        console::writeLine("  audio stop", 7, false, false);
        console::writeLine("  audio playback start", 7, false, false);
        console::writeLine("  audio playback stop", 7, false, false);
        console::writeLine("  audio both start|stop", 7, false, false);
        console::writeLine("  audio status", 7, false, false);
        console::writeLine("  audio volume <0-200>", 7, false, false);
        console::writeLine("  audio stats", 7, false, false);
        console::writeLine("  audio config", 7, false, false);
        console::writeLine("  audio devices", 7, false, false);
        console::writeLine("", 7, false, false);
    }

    console::writeLine("Совместимость со старыми командами сохранена: testFlow, testConfCreate, testChat, Прокурка чата и др.", 14, false, false);
}

void audioEventHandler(const AudioEvent& event) {
    switch (event.type) {
    case AudioEventType::STREAM_STARTED:
        console::writeLine("[audio] " + event.message, 10);
        break;
    case AudioEventType::STREAM_STOPPED:
        console::writeLine("[audio] " + event.message, 14);
        break;
    case AudioEventType::BUFFER_OVERFLOW:
    case AudioEventType::BUFFER_UNDERRUN: {
        std::ostringstream oss;
        oss << "[audio] " << event.message;
        if (event.data > 0) {
            oss << " (" << event.data << " packets)";
        }
        console::writeLine(oss.str(), 12);
        break;
    }
    case AudioEventType::ENCODING_ERROR:
    case AudioEventType::DECODING_ERROR:
    case AudioEventType::DEVICE_ERROR:
        console::writeLine("[audio.error] " + event.message, 12, true);
        break;
    case AudioEventType::VOLUME_CHANGED:
        console::writeLine("[audio] volume=" + std::to_string(event.data) + "%", 10);
        break;
    default:
        console::writeLine("[audio] " + event.message, 13);
        break;
    }
}

void doRead(websocket::stream<beast::tcp_stream>& ws) {
    beast::flat_buffer buffer;

    while (g_running) {
        try {
            ws.next_layer().expires_after(std::chrono::milliseconds(250));
            ws.read(buffer);
            const auto msg = beast::buffers_to_string(buffer.data());

            try {
                const auto json = nlohmann::json::parse(msg);
                if (json.contains("type")) {
                    const auto type = json.value("type", std::string{});

                    if (type == "audio_data" && g_audioStreamer) {
                        g_audioStreamer->processIncomingAudio(json);
                    }
                    else {
                        if ((type == "peer_assigned" || type == "dispatch_result") && json.contains("peer") && json["peer"].is_string()) {
                            setAssignedPeer(json["peer"].get<std::string>());
                        }

                        if (type == "chat_message") {
                            g_chatReceivedCounter.fetch_add(1);
                            const auto conferenceId = json.value("conferenceId", std::string{});
                            if (!conferenceId.empty()) {
                                setActiveConference(conferenceId, false);
                            }
                            const auto senderPeerId = json.value("senderPeerId", json.value("from", std::string{}));
                            if (!senderPeerId.empty()) {
                                rememberKnownConferencePeer(senderPeerId);
                            }
                            const auto targetPeer = json.value("targetPeerId", std::string{});
                            if (targetPeer.empty()) {
                                g_chatBroadcastCounter.fetch_add(1);
                            }
                            else {
                                g_chatDirectCounter.fetch_add(1);
                                rememberKnownConferencePeer(targetPeer);
                            }
                        }
                        else if (type == "dispatch_result") {
                            const auto objectType = json.value("object", std::string{});
                            const auto actionType = json.value("action", std::string{});
                            if (objectType == "chat" && actionType == "send_message") {
                                if (json.value("ok", false)) {
                                    g_chatSendAcceptedCounter.fetch_add(1);
                                }
                                else {
                                    g_chatSendRejectedCounter.fetch_add(1);
                                }
                            }
                            if (objectType == "conference" && actionType == "list_members") {
                                tryUpdatePeersFromDispatchResult(json);
                            }
                        }
                        else if (type == "audio_session_lifecycle") {
                            const bool started = json.value("started", false);
                            const bool ended = json.value("ended", false);
                            if (started) {
                                g_audioLifecycleStartedCounter.fetch_add(1);
                            }
                            if (ended) {
                                g_audioLifecycleEndedCounter.fetch_add(1);
                            }
                            if (!started && !ended) {
                                g_audioLifecycleUnknownCounter.fetch_add(1);
                            }
                        }

                        console::writeLine("< " + msg, 7);
                    }
                }
                else {
                    console::writeLine("< " + msg, 7);
                }
            }
            catch (...) {
                accountAudioLifecycleFromRawMessage(msg);
                console::writeLine("< " + msg, 7);
            }

            buffer.consume(buffer.size());
        }
        catch (const beast::system_error& se) {
            if (se.code() == beast::error::timeout) {
                if (!g_running.load()) {
                    break;
                }
                continue;
            }
            if (se.code() == websocket::error::closed
                || se.code() == net::error::eof
                || se.code() == net::error::operation_aborted
                || se.code() == net::error::interrupted) {
                console::writeLine("[ws] connection closed", 14);
                break;
            }
            fail(se.code(), "read");
            break;
        }
        catch (const std::exception& e) {
            console::writeLine(std::string("read exception: ") + e.what(), 12, true);
            break;
        }
    }
}

} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(1251);
    SetConsoleCP(1251);
#endif

    if (argc < 3) {
        console::writeLine(std::string("Usage: ") + argv[0] + " <host> <port> [options]", 12, true, false);
        console::writeLine("Options:", 12, true, false);
        console::writeLine("  --path <value>    WebSocket handshake path (default: /)", 12, true, false);
        console::writeLine("  --audio           Enable audio capture + playback", 12, true, false);
        console::writeLine("  --both            Enable both capture and playback", 12, true, false);
        console::writeLine("  --capture-only    Enable only audio capture", 12, true, false);
        console::writeLine("  --playback-only   Enable only audio playback", 12, true, false);
        console::writeLine("  --devices         Show available audio devices", 12, true, false);
        console::writeLine("  --rate <hz>       Set sample rate (default: 48000)", 12, true, false);
        console::writeLine("  --bitrate <kbps>  Set Opus bitrate (default: 64)", 12, true, false);
        return 1;
    }

    std::string host = argv[1];
    const auto port = argv[2];
    std::string wsPath = "/";
    bool enableAudio = false;
    bool enableCapture = false;
    bool enablePlayback = false;
    bool showDevicesOnly = false;
    int sampleRate = 48000;
    int bitrate = 64000;

    for (int i = 3; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--audio") {
            enableAudio = true;
            enableCapture = true;
            enablePlayback = true;
        }
        else if (arg == "--both") {
            enableAudio = true;
            enableCapture = true;
            enablePlayback = true;
        }
        else if (arg == "--capture-only") {
            enableAudio = true;
            enableCapture = true;
        }
        else if (arg == "--playback-only") {
            enableAudio = true;
            enablePlayback = true;
        }
        else if (arg == "--devices") {
            showDevicesOnly = true;
        }
        else if (arg == "--rate" && i + 1 < argc) {
            sampleRate = std::stoi(argv[++i]);
        }
        else if (arg == "--bitrate" && i + 1 < argc) {
            bitrate = std::stoi(argv[++i]) * 1000;
        }
        else if (arg == "--path" && i + 1 < argc) {
            wsPath = argv[++i];
            if (!wsPath.empty() && wsPath.front() != '/') {
                wsPath = "/" + wsPath;
            }
        }
    }

    if (showDevicesOnly) {
        showAudioDevices();
        return 0;
    }

    try {
        net::io_context ioc{ 1 };
        tcp::resolver resolver{ ioc };
        const auto results = resolver.resolve(host, port);

        beast::tcp_stream stream{ ioc };
        stream.expires_after(std::chrono::seconds(30));
        stream.connect(results);

        websocket::stream<beast::tcp_stream> ws{ std::move(stream) };
        ws.set_option(websocket::stream_base::decorator(
            [](websocket::request_type& req) {
                req.set(beast::http::field::user_agent, "EduSpaceCli/2.0");
            }));

        ws.next_layer().expires_never();
        ws.handshake(host, wsPath);
        console::writeLine("[connected] " + host + ":" + port, 10);

        if (enableAudio) {
            console::writeLine("[audio] initializing...", 11);
            g_audioStreamer = std::make_unique<AudioStreamer>(ws);

            AudioConfig config;
            config.sampleRate = sampleRate;
            config.bitrate = bitrate;
            config.frameSize = sampleRate / 50;
            g_audioStreamer->setEventCallback(audioEventHandler);

            if (g_audioStreamer->initialize(config)) {
                console::writeLine("[audio] initialized", 10);
                if (enableCapture) {
                    g_audioStreamer->startCapture();
                }
                if (enablePlayback) {
                    g_audioStreamer->startPlayback();
                }
            }
            else {
                console::writeLine("[audio] failed to initialize", 12, true);
                g_audioStreamer.reset();
            }
        }

        printHelp(enableAudio);
        std::thread reader([&ws] { doRead(ws); });

        std::string line;
        while (g_running && std::getline(std::cin, line)) {
            if (line.empty()) {
                continue;
            }

            if (line == "Прокурка чата") {
                if (!runChatCycle(ws, {}, "Прокурка чата: broadcast")) {
                    g_running = false;
                }
                continue;
            }
            if (line == "статистика сообщений") {
                printChatStats();
                continue;
            }

            std::istringstream iss(line);
            std::string cmd;
            iss >> cmd;

            if (cmd == "quit" || cmd == "exit" || cmd == "q") {
                g_running = false;
                break;
            }
            if (cmd == "help" || cmd == "?") {
                printHelp(enableAudio);
                continue;
            }
            if (cmd == "send") {
                std::string rest;
                std::getline(iss >> std::ws, rest);
                if (rest.empty()) {
                    console::writeLine("Usage: send {\"object\":\"conference\",\"action\":\"get_conference\",\"ctx\":{\"conferenceId\":\"id\"}}", 14);
                    continue;
                }
                if (!sendText(ws, rest)) {
                    break;
                }
                continue;
            }
            if (cmd == "chat.create") {
                std::string conferenceId;
                std::getline(iss >> std::ws, conferenceId);
                if (conferenceId.empty()) {
                    conferenceId = nextClientRequestId("conf-live");
                }
                if (!sendConferenceAction(ws, "create_conference", nlohmann::json{ {"conferenceId", conferenceId} })) {
                    break;
                }
                setActiveConference(conferenceId);
                if (!requestConferenceMembers(ws, conferenceId)) {
                    break;
                }
                continue;
            }
            if (cmd == "chat.join") {
                std::string conferenceId;
                iss >> conferenceId;
                if (conferenceId.empty()) {
                    console::writeLine("Usage: chat.join <conferenceId>", 14);
                    continue;
                }
                if (!sendConferenceAction(ws, "join_conference", nlohmann::json{ {"conferenceId", conferenceId} })) {
                    break;
                }
                setActiveConference(conferenceId);
                if (!requestConferenceMembers(ws, conferenceId)) {
                    break;
                }
                continue;
            }
            if (cmd == "chat.use") {
                std::string conferenceId;
                iss >> conferenceId;
                if (conferenceId.empty()) {
                    console::writeLine("Usage: chat.use <conferenceId>", 14);
                    continue;
                }
                setActiveConference(conferenceId);
                continue;
            }
            if (cmd == "chat.members") {
                std::string conferenceId;
                iss >> conferenceId;
                if (conferenceId.empty()) {
                    conferenceId = getActiveConference();
                }
                if (conferenceId.empty()) {
                    console::writeLine("Usage: chat.members [conferenceId]", 14);
                    continue;
                }
                setActiveConference(conferenceId, false);
                if (!requestConferenceMembers(ws, conferenceId)) {
                    break;
                }
                continue;
            }
            if (cmd == "chat.peers") {
                printKnownConferencePeers();
                continue;
            }
            if (cmd == "chat.active") {
                printChatContextStatus();
                continue;
            }
            if (cmd == "msg") {
                std::string text;
                std::getline(iss >> std::ws, text);
                if (text.empty()) {
                    console::writeLine("Usage: msg <text>", 14);
                    continue;
                }
                if (!sendMessageToActiveConference(ws, text)) {
                    break;
                }
                continue;
            }
            if (cmd == "msg.to") {
                std::string targetPeerId;
                iss >> targetPeerId;
                std::string text;
                std::getline(iss >> std::ws, text);
                if (targetPeerId.empty() || text.empty()) {
                    console::writeLine("Usage: msg.to <peerId> <text>", 14);
                    continue;
                }
                if (!sendMessageToActiveConference(ws, text, targetPeerId)) {
                    break;
                }
                continue;
            }
            if (cmd == "test.mediasoup" || cmd == "testFlow") {
                std::string roomId;
                iss >> roomId;
                if (!runMediasoupFullCycle(ws, roomId)) {
                    break;
                }
                continue;
            }
            if (cmd == "test.conference") {
                std::string conferenceId;
                iss >> conferenceId;
                if (!runConferenceCycle(ws, conferenceId)) {
                    break;
                }
                continue;
            }
            if (cmd == "test.chat" || cmd == "testChatSmoke") {
                std::string conferenceId;
                iss >> conferenceId;
                std::string text;
                std::getline(iss >> std::ws, text);
                if (!runChatCycle(ws, conferenceId, text)) {
                    break;
                }
                continue;
            }
            if (cmd == "test.full") {
                std::string roomId;
                std::string conferenceId;
                iss >> roomId >> conferenceId;
                std::string text;
                std::getline(iss >> std::ws, text);
                if (!runFullScenario(ws, roomId, conferenceId, text)) {
                    break;
                }
                continue;
            }
            if (cmd == "test.audio" || cmd == "testAudio") {
                std::string roomId;
                iss >> roomId;
                if (!runAudioCheckCycle(ws, roomId)) {
                    break;
                }
                continue;
            }
            if (cmd == "test.chat.stats" || cmd == "chat.stats") {
                std::string option;
                iss >> option;
                if (option == "reset" || option == "clear") {
                    resetChatStats();
                    console::writeLine("[test.chat.stats] counters reset", 10);
                }
                printChatStats();
                continue;
            }
            if (cmd == "test.audio.stats") {
                std::string option;
                iss >> option;
                if (option == "reset" || option == "clear") {
                    resetAudioLifecycleStats();
                    console::writeLine("[test.audio.stats] counters reset", 10);
                }
                printAudioLifecycleStats();
                continue;
            }

            if (enableAudio && cmd == "audio") {
                if (!g_audioStreamer) {
                    console::writeLine("Audio not initialized", 14);
                    continue;
                }

                std::string subcmd;
                iss >> subcmd;
                if (subcmd == "start") {
                    g_audioStreamer->startCapture();
                }
                else if (subcmd == "stop") {
                    g_audioStreamer->stopCapture();
                }
                else if (subcmd == "playback") {
                    std::string playcmd;
                    iss >> playcmd;
                    if (playcmd == "start") {
                        g_audioStreamer->startPlayback();
                    }
                    else if (playcmd == "stop") {
                        g_audioStreamer->stopPlayback();
                    }
                    else {
                        console::writeLine("Usage: audio playback <start|stop>", 14);
                    }
                }
                else if (subcmd == "both") {
                    std::string bothCmd;
                    iss >> bothCmd;
                    if (bothCmd == "start") {
                        g_audioStreamer->startCapture();
                        g_audioStreamer->startPlayback();
                    }
                    else if (bothCmd == "stop") {
                        g_audioStreamer->stopCapture();
                        g_audioStreamer->stopPlayback();
                    }
                    else {
                        console::writeLine("Usage: audio both <start|stop>", 14);
                    }
                }
                else if (subcmd == "status") {
                    std::ostringstream oss;
                    oss << "[audio.status] state=" << g_audioStreamer->getStateString();
                    console::writeLine(oss.str(), 11);
                }
                else if (subcmd == "volume") {
                    int vol = 0;
                    if (iss >> vol) {
                        g_audioStreamer->setVolume(vol / 100.0f);
                    }
                    else {
                        console::writeLine("Usage: audio volume <0-200>", 14);
                    }
                }
                else if (subcmd == "stats") {
                    printAudioStats();
                }
                else if (subcmd == "config") {
                    const auto cfg = g_audioStreamer->getConfig();
                    console::writeLine("[audio.config] sampleRate=" + std::to_string(cfg.sampleRate), 11);
                    console::writeLine("[audio.config] channels=" + std::to_string(cfg.channels), 11);
                    console::writeLine("[audio.config] frameSize=" + std::to_string(cfg.frameSize), 11);
                    console::writeLine("[audio.config] bitrate=" + std::to_string(cfg.bitrate), 11);
                    console::writeLine("[audio.config] volume=" + std::to_string(static_cast<int>(cfg.volume * 100)) + "%", 11);
                }
                else if (subcmd == "devices") {
                    showAudioDevices();
                }
                else {
                    console::writeLine("Unknown audio command", 14);
                }
                continue;
            }

            if (cmd == "ms.create" || cmd == "testCreate") {
                std::string roomId;
                std::getline(iss >> std::ws, roomId);
                if (roomId.empty()) roomId = "room-cli";
                if (!sendMediasoupAction(ws, "create_room", nlohmann::json{ {"roomId", roomId} })) break;
                continue;
            }
            if (cmd == "ms.join" || cmd == "testJoin") {
                std::string roomId;
                iss >> roomId;
                if (roomId.empty()) {
                    console::writeLine("Usage: ms.join <roomId>", 14);
                    continue;
                }
                if (!sendMediasoupAction(ws, "join_room", nlohmann::json{ {"roomId", roomId} })) break;
                continue;
            }
            if (cmd == "ms.transport") {
                std::string roomId;
                std::string transportId;
                iss >> roomId >> transportId;
                if (roomId.empty()) {
                    console::writeLine("Usage: ms.transport <roomId> [transportId]", 14);
                    continue;
                }
                if (transportId.empty()) transportId = "transport-cli-01";
                if (!sendMediasoupAction(ws, "open_transport", nlohmann::json{
                    {"roomId", roomId},
                    {"transportId", transportId}
                })) break;
                continue;
            }
            if (cmd == "ms.produce") {
                std::string roomId;
                std::string transportId;
                std::string producerId;
                std::string kind;
                iss >> roomId >> transportId >> producerId >> kind;
                if (roomId.empty()) {
                    console::writeLine("Usage: ms.produce <roomId> [transportId] [producerId] [kind]", 14);
                    continue;
                }
                if (transportId.empty()) transportId = "transport-cli-01";
                if (producerId.empty()) producerId = "producer-audio-cli-01";
                if (kind.empty()) kind = "audio";
                if (!sendMediasoupAction(ws, "produce", nlohmann::json{
                    {"roomId", roomId},
                    {"transportId", transportId},
                    {"producerId", producerId},
                    {"kind", kind}
                })) break;
                continue;
            }
            if (cmd == "ms.offer" || cmd == "testOffer") {
                std::string sdp;
                std::getline(iss >> std::ws, sdp);
                if (sdp.empty()) sdp = kDefaultSdpOffer;
                if (!sendMediasoupAction(ws, "webrtc_offer", nlohmann::json{ {"sdp", sdp} })) break;
                continue;
            }
            if (cmd == "ms.ice" || cmd == "testIce") {
                std::string sdpMid;
                iss >> sdpMid;
                std::string candidate;
                std::getline(iss >> std::ws, candidate);
                if (sdpMid.empty()) sdpMid = "0";
                if (candidate.empty()) candidate = "candidate:0 1 UDP 2122252543 127.0.0.1 5000 typ host";
                if (!sendMediasoupAction(ws, "webrtc_ice", nlohmann::json{
                    {"sdpMid", sdpMid},
                    {"candidate", candidate}
                })) break;
                continue;
            }
            if (cmd == "ms.close" || cmd == "testClose") {
                if (!sendMediasoupAction(ws, "webrtc_close")) break;
                continue;
            }

            if (cmd == "conf.create" || cmd == "testConfCreate") {
                std::string conferenceId;
                std::getline(iss >> std::ws, conferenceId);
                if (conferenceId.empty()) conferenceId = "conference-cli";
                if (!sendConferenceAction(ws, "create_conference", nlohmann::json{ {"conferenceId", conferenceId} })) break;
                setActiveConference(conferenceId);
                if (!requestConferenceMembers(ws, conferenceId)) break;
                continue;
            }
            if (cmd == "conf.get" || cmd == "testConfGet") {
                std::string conferenceId;
                iss >> conferenceId;
                if (conferenceId.empty()) {
                    console::writeLine("Usage: conf.get <conferenceId>", 14);
                    continue;
                }
                if (!sendConferenceAction(ws, "get_conference", nlohmann::json{ {"conferenceId", conferenceId} })) break;
                continue;
            }
            if (cmd == "conf.join" || cmd == "testConfJoin") {
                std::string conferenceId;
                iss >> conferenceId;
                if (conferenceId.empty()) {
                    console::writeLine("Usage: conf.join <conferenceId>", 14);
                    continue;
                }
                if (!sendConferenceAction(ws, "join_conference", nlohmann::json{ {"conferenceId", conferenceId} })) break;
                setActiveConference(conferenceId);
                if (!requestConferenceMembers(ws, conferenceId)) break;
                continue;
            }
            if (cmd == "conf.leave" || cmd == "testConfLeave") {
                std::string conferenceId;
                iss >> conferenceId;
                if (conferenceId.empty()) {
                    console::writeLine("Usage: conf.leave <conferenceId>", 14);
                    continue;
                }
                if (!sendConferenceAction(ws, "leave_conference", nlohmann::json{ {"conferenceId", conferenceId} })) break;
                continue;
            }
            if (cmd == "conf.close" || cmd == "testConfClose") {
                std::string conferenceId;
                iss >> conferenceId;
                if (conferenceId.empty()) {
                    console::writeLine("Usage: conf.close <conferenceId>", 14);
                    continue;
                }
                if (!sendConferenceAction(ws, "close_conference", nlohmann::json{ {"conferenceId", conferenceId} })) break;
                continue;
            }
            if (cmd == "conf.members" || cmd == "testConfMembers") {
                std::string conferenceId;
                iss >> conferenceId;
                if (conferenceId.empty()) {
                    conferenceId = getActiveConference();
                }
                if (conferenceId.empty()) {
                    console::writeLine("Usage: conf.members [conferenceId]", 14);
                    continue;
                }
                setActiveConference(conferenceId, false);
                if (!requestConferenceMembers(ws, conferenceId)) break;
                continue;
            }

            if (cmd == "chat.send" || cmd == "testChat") {
                std::string conferenceId;
                iss >> conferenceId;
                std::string text;
                std::getline(iss >> std::ws, text);
                if (conferenceId.empty() || text.empty()) {
                    console::writeLine("Usage: chat.send <conferenceId> <text>", 14);
                    continue;
                }
                setActiveConference(conferenceId, false);
                if (!sendChatAction(ws, "send_message", nlohmann::json{
                    {"conferenceId", conferenceId},
                    {"text", text}
                })) break;
                continue;
            }
            if (cmd == "chat.to" || cmd == "testChatTo") {
                std::string conferenceId;
                std::string targetPeerId;
                iss >> conferenceId >> targetPeerId;
                std::string text;
                std::getline(iss >> std::ws, text);
                if (conferenceId.empty() || targetPeerId.empty() || text.empty()) {
                    console::writeLine("Usage: chat.to <conferenceId> <peerId> <text>", 14);
                    continue;
                }
                setActiveConference(conferenceId, false);
                rememberKnownConferencePeer(targetPeerId);
                if (!sendChatAction(ws, "send_message", nlohmann::json{
                    {"conferenceId", conferenceId},
                    {"targetPeerId", targetPeerId},
                    {"text", text}
                })) break;
                continue;
            }

            if (cmd == "testProkyrka" || cmd == "testProkurka") {
                std::string roomId;
                iss >> roomId;
                if (roomId.empty()) roomId = "room-cli";
                if (!sendMediasoupAction(ws, "create_room", nlohmann::json{ {"roomId", roomId} })) break;
                sleepStep();
                if (!sendMediasoupAction(ws, "join_room", nlohmann::json{ {"roomId", roomId} })) break;
                continue;
            }

            console::writeLine("Unknown command. Type 'help' for commands.", 14);
        }

        g_running = false;
        if (g_audioStreamer) {
            g_audioStreamer->shutdown();
        }

        try {
            beast::error_code cancelEc;
            ws.next_layer().socket().cancel(cancelEc);

            if (reader.joinable()) {
                reader.join();
            }

            beast::error_code socketCloseEc;
            ws.next_layer().socket().close(socketCloseEc);
        }
        catch (const std::exception& shutdownError) {
            console::writeLine(std::string("[shutdown] ") + shutdownError.what(), 12, true);
        }
    }
    catch (const beast::system_error& se) {
        fail(se.code(), "main");
        return 1;
    }
    catch (const std::exception& e) {
        console::writeLine(std::string("Exception: ") + e.what(), 12, true);
        return 1;
    }

    console::writeLine("[disconnected]", 14);
    return 0;
}
