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
    wsServer.fnSetOnMessage([](const std::string& msg, void* session) {
        std::cout << "[WS] message: " << msg << std::endl;
        });
    wsServer.fnStart();

    // RTC Manager
    cRtcManager rtcMgr([&wsServer](void* session, const std::string& sMsg) {
        // Отправляем клиенту через WS
        // std::cout << "[RTC->WS] " << sMsg << std::endl;
        });
    rtcMgr.fnInit();

    // Аудио устройство
    cMediaAudioDevice mic;
    cMediaOpusEncoder opusEnc(48000, 1);

    mic.fnSetCallback([&](const std::vector<int16_t>& samples) {
        auto encoded = opusEnc.fnEncode(samples);
        std::cout << "[Audio] Captured " << samples.size() * sizeof(int16_t)
            << " bytes, encoded to " << encoded.size() << " bytes\n";
        });

    mic.fnStartCapture();

    std::cout << "Server running... Press Enter to quit.\n";
    std::cin.get();

    mic.fnStopCapture();
    ioCtx.fnStop();
    httpServer.fnStop();
    wsServer.fnStop();

    return 0;
}
