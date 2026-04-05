#pragma once

#include "Bridge/Mediasoup/service/IMediaEngine.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/websocket/stream.hpp>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace eds::server_new::mediasoup::service {

class MediasoupSfuEngine final : public IMediaEngine {
public:
    explicit MediasoupSfuEngine(bool debugMode = false);
    ~MediasoupSfuEngine() override;

    core::contracts::OperationStatus execute(
        MediaTransportIntent intent,
        const MediaTransportCommand& command,
        std::vector<MediaTransportEvent>& emittedEvents) override;

    std::vector<MediaSignalingEvent> consumeSignalingEventsForPeer(std::string_view peerId) override;

    void onSessionDisconnected(
        std::string_view peerId,
        std::uintptr_t sessionHandle,
        std::vector<MediaTransportEvent>& emittedEvents) override;

private:
    using tcp = boost::asio::ip::tcp;
    using ws_stream = boost::beast::websocket::stream<boost::beast::tcp_stream>;

    struct RoomState {
        std::unordered_set<std::string> peers;
    };

    struct PeerState {
        std::string roomId;
        std::uintptr_t sessionHandle = 0;
    };

    struct TransportState {
        std::string roomId;
        std::string peerId;
    };

    struct ProducerState {
        std::string roomId;
        std::string peerId;
        std::string transportId;
        std::string kind;
    };

private:
    static core::contracts::OperationStatus requireField(bool condition, std::string fieldName);
    static MediaTransportEvent makeErrorEvent(const MediaTransportCommand& command, std::string reason);
    static bool parseBackendUrl(
        std::string_view url,
        std::string& host,
        std::string& port,
        std::string& path);
    static std::string defaultBackendUrl();
    static std::string resolveOperationName(MediaTransportIntent intent);
    static bool isMediasoupEngineName(std::string_view value);
    void refreshBackendEndpointNoLock();

    core::contracts::OperationStatus ensureConnectedNoLock();
    core::contracts::OperationStatus verifyMediasoupBackendNoLock();
    void disconnectNoLock();
    core::contracts::OperationStatus executeIntentNoLock(
        MediaTransportIntent intent,
        const MediaTransportCommand& command,
        std::vector<MediaTransportEvent>& emittedEvents);
    core::contracts::OperationStatus callMediasoupBackendNoLock(
        const std::string& operationName,
        const MediaTransportCommand& command,
        std::string& backendMessage);
    void appendSignalingEventsFromBackendNoLock(
        std::string_view fallbackPeerId,
        std::string_view backendPayloadText);
    std::vector<std::string> collectRoomPeersNoLock(std::string_view roomId) const;
    void clearPeerRuntimeNoLock(std::string_view roomId, std::string_view peerId);
    core::contracts::OperationStatus leaveSessionNoLock(
        const MediaTransportCommand& command,
        std::vector<MediaTransportEvent>& emittedEvents,
        std::string reason,
        std::string backendOperation);

private:
    bool debugMode_ = false;
    std::string backendUrl_;
    std::string backendHost_;
    std::string backendPort_;
    std::string backendPath_ = "/";
    bool backendConnected_ = false;
    bool backendVerified_ = false;
    std::string backendEngine_;
    std::string backendVersion_;

    boost::asio::io_context ioContext_;
    std::unique_ptr<tcp::resolver> resolver_;
    std::unique_ptr<ws_stream> socket_;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, RoomState> rooms_;
    std::unordered_map<std::string, PeerState> peers_;
    std::unordered_map<std::string, TransportState> transports_;
    std::unordered_map<std::string, ProducerState> producers_;
    std::unordered_map<std::string, std::vector<MediaSignalingEvent>> signalingByPeer_;
    std::unordered_map<std::string, std::uint64_t> backendOperationCounters_;
};

} // namespace eds::server_new::mediasoup::service
