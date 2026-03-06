#include "App/ApplicationCore.h"
#include "Bridge/Mediasoup/signaling/MediasoupSignalingGateway.h"
#include "Bridge/Mediasoup/runtime/MediasoupCommand.h"

#include <iostream>
#include <string>

namespace {

bool dispatch(
    ApplicationApi& app,
    std::string action,
    const eds::server_new::mediasoup::MediasoupCommand& command) {
    const core::contracts::MessageRoute route{
        std::string(eds::server_new::mediasoup::kRouteObject),
        std::string(eds::server_new::mediasoup::kDefaultAgent),
        std::move(action)
    };

    const auto status = app.dispatchMediasoup(route, command);
    if (!status.ok) {
        std::cerr << "[mediasoup] action '" << route.action << "' failed: " << status.message << '\n';
        return false;
    }

    std::cout << "[mediasoup] action '" << route.action << "' success\n";
    return true;
}

}

int main(int argc, char** argv) {
    constexpr unsigned short kDefaultWsPort = 9002;
    ApplicationApi app;
    if (!app.init()) {
        std::cerr << "Failed to initialize application.\n";
        return 1;
    }

    if (argc > 1 && std::string(argv[1]) == "--server") {
        eds::server_new::mediasoup::signaling::MediasoupSignalingGateway gateway(app, kDefaultWsPort);
        if (!gateway.start()) {
            std::cerr << "Failed to start Mediasoup signaling gateway.\n";
            return 1;
        }

        std::cout << "[mediasoup] signaling gateway started on ws://0.0.0.0:" << kDefaultWsPort << '\n';
        gateway.wait();
        return 0;
    }

    eds::server_new::mediasoup::MediasoupCommand createRoom;
    createRoom.roomId = "room-001";
    if (!dispatch(app, std::string(eds::server_new::mediasoup::kActionCreateRoom), createRoom)) {
        return 1;
    }

    eds::server_new::mediasoup::MediasoupCommand aliceJoin;
    aliceJoin.roomId = "room-001";
    aliceJoin.peerId = "peer-alice";
    if (!dispatch(app, std::string(eds::server_new::mediasoup::kActionJoinRoom), aliceJoin)) {
        return 1;
    }

    eds::server_new::mediasoup::MediasoupCommand aliceTransport;
    aliceTransport.roomId = "room-001";
    aliceTransport.peerId = "peer-alice";
    aliceTransport.transportId = "transport-01";
    if (!dispatch(app, std::string(eds::server_new::mediasoup::kActionOpenTransport), aliceTransport)) {
        return 1;
    }

    eds::server_new::mediasoup::MediasoupCommand aliceProduce;
    aliceProduce.roomId = "room-001";
    aliceProduce.peerId = "peer-alice";
    aliceProduce.transportId = "transport-01";
    aliceProduce.producerId = "producer-audio-01";
    aliceProduce.kind = "audio";
    if (!dispatch(app, std::string(eds::server_new::mediasoup::kActionProduce), aliceProduce)) {
        return 1;
    }

    eds::server_new::mediasoup::MediasoupCommand bobJoin;
    bobJoin.roomId = "room-001";
    bobJoin.peerId = "peer-bob";
    if (!dispatch(app, std::string(eds::server_new::mediasoup::kActionJoinRoom), bobJoin)) {
        return 1;
    }

    eds::server_new::mediasoup::MediasoupCommand bobConsume;
    bobConsume.roomId = "room-001";
    bobConsume.peerId = "peer-bob";
    bobConsume.producerId = "producer-audio-01";
    if (!dispatch(app, std::string(eds::server_new::mediasoup::kActionConsume), bobConsume)) {
        return 1;
    }

    return app.start() ? 0 : 1;
}
