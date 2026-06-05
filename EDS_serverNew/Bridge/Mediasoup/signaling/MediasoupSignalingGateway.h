#pragma once

#include "Bridge/Mediasoup/runtime/MediasoupCommand.h"
#include "Bridge/Mediasoup/transport/NetIoContext.h"
#include "Bridge/Mediasoup/transport/WebSocketServer.h"
#include "features/events/AudioSessionEvents.h"
#include "features/runtime/FeatureEventBus.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>
#include <boost/signals2/connection.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

class ApplicationApi;

namespace eds::server_new::mediasoup::signaling {

    class MediasoupSignalingGateway {
    public:
        MediasoupSignalingGateway(
            ApplicationApi& app,
            unsigned short wsPort,
            bool allowDirectMediasoupDebug = false,
            bool debugMode = false,
            transport::WebSocketTlsOptions tlsOptions = {});

        ~MediasoupSignalingGateway();

        bool start();
        void stop();
        void wait();

    private:
        using SendStrand = boost::asio::strand<boost::asio::io_context::executor_type>;

    private:
        void onConnected(void* session);
        void onDisconnected(void* session);
        void onMessage(const std::string& text, void* session);
        void onAudioSessionLifecycleEvent(
            const eds::server_new::features::events::AudioSessionLifecycleEvent& event);

        std::string resolveTrustedPeer(void* session);

        void postText(void* session, std::string text);
        void postTexts(void* session, std::vector<std::string> texts);
        void postTextToPeer(std::string peerId, std::string text);
        void postTextToPeers(std::vector<std::string> peerIds, std::string text);
        bool isPeerConnected(std::string_view peerId);
        void loadRuntimeLimitsFromEnvironment();

        void startOfflineOutboxDispatcher();
        void stopOfflineOutboxDispatcher();
        void runOfflineOutboxDispatcher();

    private:
        ApplicationApi& app_;
        unsigned short wsPort_ = 0;
        bool allowDirectMediasoupDebug_ = false;
        bool debugMode_ = false;
        transport::WebSocketTlsOptions tlsOptions_;

        transport::NetIoContext ioContext_;
        std::unique_ptr<transport::WebSocketServer> wsServer_;
        std::unique_ptr<SendStrand> sendStrand_;

        std::atomic<bool> running_{ false };
        std::atomic<std::uint64_t> receivedMessages_{ 0 };
        std::atomic<std::uint64_t> forwardedEvents_{ 0 };

        std::shared_ptr<eds::server_new::features::runtime::FeatureEventBus> featureEventBus_;
        boost::signals2::scoped_connection audioSessionLifecycleSubscription_;

        std::mutex peersMutex_;
        std::unordered_map<void*, std::string> sessionToPeer_;
        std::unordered_map<std::string, void*> peerToSession_;
        std::size_t maxConcurrentConnections_ = 5000;
        std::uint64_t maxMessagesPerSecondPerPeer_ = 100;
        unsigned int ioThreadCount_ = 0;
        struct PeerRateState {
            std::uint64_t messageCount = 0;
            std::chrono::steady_clock::time_point windowStart = std::chrono::steady_clock::now();
        };
        std::unordered_map<void*, PeerRateState> peerRateState_;

        std::atomic<bool> outboxDispatcherStop_{ false };
        std::thread outboxDispatcherThread_;
    };

} // namespace eds::server_new::mediasoup::signaling