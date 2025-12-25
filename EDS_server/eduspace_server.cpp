#include "includes.h"

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

    // WebSocket сервер
    cNetWebSocketServer wsServer(ioCtx.fnIo(), 9000);
    wsServer.fnSetOnConnected([](void* session) {
        std::cout << "[WS] client connected: " << session << std::endl;
        });
    wsServer.fnSetOnDisconnected([](void* session) {
        std::cout << "[WS] client disconnected: " << session << std::endl;
        });
    wsServer.fnSetOnMessage([&wsServer](const std::string& msg, void* session) {
        std::cout << "[WS] text message from " << session << ": " << msg << std::endl;
        wsServer.fnBroadcastText(msg, session);
        });
    wsServer.fnSetOnBinary([&wsServer](const std::vector<uint8_t>& data, void* session) {
        // Broadcast raw audio frames from one client to everyone else
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
