#include "App/ApplicationCore.h"
#include "Bridge/Mediasoup/runtime/MediasoupBackendReadinessProbe.h"
#include "Bridge/Mediasoup/runtime/MediasoupBackendSupervisor.h"
#include "Bridge/Mediasoup/runtime/MediasoupDebugConfig.h"
#include "Bridge/Mediasoup/signaling/MediasoupSignalingGateway.h"
#include "Bridge/Mediasoup/runtime/MediasoupCommand.h"
#include <atomic>
#include <chrono>
#include <cstdlib>

#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

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
std::string readEnvVar(const char* name) {
    const auto value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return {};
    }
    return std::string(value);
}

bool setEnvVar(const char* name, const std::string& value, std::string& error) {
    error.clear();
#ifdef _WIN32
    if (_putenv_s(name, value.c_str()) != 0) {
        error = std::string("Failed to set environment variable '") + name + "'.";
        return false;
    }
    return true;
#else
    if (setenv(name, value.c_str(), 1) != 0) {
        error = std::string("Failed to set environment variable '") + name + "'.";
        return false;
    }
    return true;
#endif
}

bool parsePositiveIntOption(const std::string& value, int& result) {
    try {
        const auto parsed = std::stoi(value);
        if (parsed <= 0) {
            return false;
        }
        result = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

bool waitForBackendReadiness(
    std::string_view backendUrl,
    eds::server_new::mediasoup::runtime::MediasoupBackendSupervisor& supervisor,
    std::chrono::milliseconds readyTimeout,
    std::string& error) {
    error.clear();
    const auto deadline = std::chrono::steady_clock::now() + readyTimeout;
    int attempt = 1;
    std::string lastProbeError;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto childExit = supervisor.pollUnexpectedExit();
        if (childExit.has_value()) {
            error = childExit->reason;
            if (!childExit->recentOutput.empty()) {
                error += "\nChild output:\n" + childExit->recentOutput;
            }
            return false;
        }

        const auto probeResult = eds::server_new::mediasoup::runtime::MediasoupBackendReadinessProbe::probe(
            backendUrl,
            std::chrono::milliseconds(1200));
        if (probeResult.ok) {
            std::cout << "[mediasoup][dev-supervisor] backend ready"
                      << " engine=" << probeResult.engine
                      << " version=" << (probeResult.version.empty() ? "unknown" : probeResult.version)
                      << "\n";
            return true;
        }
        lastProbeError = probeResult.message;
        std::cout << "[mediasoup][dev-supervisor] readiness attempt #"
                  << attempt
                  << " failed: "
                  << probeResult.message
                  << "\n";
        ++attempt;
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    error = "Timed out waiting for mediasoup backend readiness.";
    if (!lastProbeError.empty()) {
        error += " Last probe error: " + lastProbeError;
    }
    const auto recentOutput = supervisor.recentOutputSnapshot();
    if (!recentOutput.empty()) {
        error += "\nChild output:\n" + recentOutput;
    }
    return false;
}

}

int main(int argc, char** argv) {
    constexpr unsigned short kDefaultWsPort = 9002;
    bool runServer = false;
    bool allowDirectMediasoupDebug = false;
    bool debugMode = false;
    bool devAutostartBackend = true;
    std::string devBackendCommand;
    std::string devBackendUrl;
    int devReadyTimeoutMs = 20000;
    int devStopTimeoutMs = 5000;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "-d"){//Базовый аргумент запуска отладки
            runServer = true;
            debugMode = true;
            devAutostartBackend = true;
            devBackendCommand = "";
            devBackendUrl = "";
            break;
        }
        if (arg == "--server") {
            runServer = true;
            continue;
        }
        if (arg == "--allow-direct-mediasoup") {
            allowDirectMediasoupDebug = true;
            continue;
        }
        if (arg == "--debug") {
            debugMode = true;
            continue;
        }
        if (arg == "--dev-autostart-mediasoup-backend") {
            devAutostartBackend = true;
            continue;
        }
        if (arg == "--mediasoup-backend-cmd") {
            if (i + 1 >= argc) {
                std::cerr << "--mediasoup-backend-cmd requires value.\n";
                return 1;
            }
            devBackendCommand = argv[++i];
            continue;
        }
        if (arg.rfind("--mediasoup-backend-cmd=", 0) == 0) {
            devBackendCommand = arg.substr(std::string("--mediasoup-backend-cmd=").size());
            continue;
        }
        if (arg == "--mediasoup-backend-url") {
            if (i + 1 >= argc) {
                std::cerr << "--mediasoup-backend-url requires value.\n";
                return 1;
            }
            devBackendUrl = argv[++i];
            continue;
        }
        if (arg.rfind("--mediasoup-backend-url=", 0) == 0) {
            devBackendUrl = arg.substr(std::string("--mediasoup-backend-url=").size());
            continue;
        }
        if (arg == "--mediasoup-backend-ready-timeout-ms") {
            if (i + 1 >= argc) {
                std::cerr << "--mediasoup-backend-ready-timeout-ms requires value.\n";
                return 1;
            }
            if (!parsePositiveIntOption(argv[++i], devReadyTimeoutMs)) {
                std::cerr << "--mediasoup-backend-ready-timeout-ms must be a positive integer.\n";
                return 1;
            }
            continue;
        }
        if (arg.rfind("--mediasoup-backend-ready-timeout-ms=", 0) == 0) {
            const auto value = arg.substr(std::string("--mediasoup-backend-ready-timeout-ms=").size());
            if (!parsePositiveIntOption(value, devReadyTimeoutMs)) {
                std::cerr << "--mediasoup-backend-ready-timeout-ms must be a positive integer.\n";
                return 1;
            }
            continue;
        }
        if (arg == "--mediasoup-backend-stop-timeout-ms") {
            if (i + 1 >= argc) {
                std::cerr << "--mediasoup-backend-stop-timeout-ms requires value.\n";
                return 1;
            }
            if (!parsePositiveIntOption(argv[++i], devStopTimeoutMs)) {
                std::cerr << "--mediasoup-backend-stop-timeout-ms must be a positive integer.\n";
                return 1;
            }
            continue;
        }
        if (arg.rfind("--mediasoup-backend-stop-timeout-ms=", 0) == 0) {
            const auto value = arg.substr(std::string("--mediasoup-backend-stop-timeout-ms=").size());
            if (!parsePositiveIntOption(value, devStopTimeoutMs)) {
                std::cerr << "--mediasoup-backend-stop-timeout-ms must be a positive integer.\n";
                return 1;
            }
            continue;
        }
        std::cerr << "Unknown argument: " << arg << "\n";
        std::cerr << "Supported flags: --server, --allow-direct-mediasoup, --debug, "
                  << "--dev-autostart-mediasoup-backend, "
                  << "--mediasoup-backend-cmd, --mediasoup-backend-url, "
                  << "--mediasoup-backend-ready-timeout-ms, --mediasoup-backend-stop-timeout-ms\n";
        return 1;
    }
    if (!runServer && allowDirectMediasoupDebug) {
        std::cerr << "--allow-direct-mediasoup requires --server mode.\n";
        return 1;
    }
    if (!runServer && debugMode) {
        std::cerr << "--debug requires --server mode.\n";
        return 1;
    }
    if (!runServer && devAutostartBackend) {
        std::cerr << "--dev-autostart-mediasoup-backend requires --server mode.\n";
        return 1;
    }
    if (!runServer && (!devBackendCommand.empty() || !devBackendUrl.empty())) {
        std::cerr << "--mediasoup-backend-* options require --server mode.\n";
        return 1;
    }

    if (runServer) {
        if (devBackendUrl.empty()) {
            devBackendUrl = readEnvVar("EDUSPACE_MEDIASOUP_BACKEND_URL");
        }
        if (!devBackendUrl.empty()) {
            std::string setEnvError;
            if (!setEnvVar("EDUSPACE_MEDIASOUP_BACKEND_URL", devBackendUrl, setEnvError)) {
                std::cerr << "[mediasoup][bootstrap] " << setEnvError << "\n";
                return 1;
            }
        }
    }

    eds::server_new::mediasoup::debug::setServerDebugEnabled(debugMode);
    ApplicationApi app;
    if (!app.init()) {
        std::cerr << "Failed to initialize application.\n";
        return 1;
    }

    if (runServer) {
        std::optional<eds::server_new::mediasoup::runtime::MediasoupBackendSupervisor> backendSupervisor;
        const auto stopTimeout = std::chrono::milliseconds(devStopTimeoutMs);

        if (devAutostartBackend) {
            if (devBackendCommand.empty()) {
                devBackendCommand = readEnvVar("EDUSPACE_MEDIASOUP_BACKEND_CMD");
            }
            if (devBackendCommand.empty()) {
                std::cerr << "[mediasoup][dev-supervisor] backend command is not configured. "
                             "Use --mediasoup-backend-cmd or EDUSPACE_MEDIASOUP_BACKEND_CMD.\n";
                return 1;
            }

            if (devBackendUrl.empty()) {
                devBackendUrl = readEnvVar("EDUSPACE_MEDIASOUP_BACKEND_URL");
            }
            if (devBackendUrl.empty()) {
                std::cerr << "[mediasoup][dev-supervisor] backend URL is not configured. "
                             "Use --mediasoup-backend-url or EDUSPACE_MEDIASOUP_BACKEND_URL.\n";
                return 1;
            }

            std::string setEnvError;
            if (!setEnvVar("EDUSPACE_MEDIASOUP_BACKEND_URL", devBackendUrl, setEnvError)) {
                std::cerr << "[mediasoup][dev-supervisor] " << setEnvError << "\n";
                return 1;
            }

            backendSupervisor.emplace();
            std::string startError;
            std::cout << "[mediasoup][dev-supervisor] starting backend child process.\n";
            if (!backendSupervisor->start(devBackendCommand, startError)) {
                std::cerr << "[mediasoup][dev-supervisor] failed to start backend: "
                          << startError
                          << "\n";
                return 1;
            }

            std::string readinessError;
            if (!waitForBackendReadiness(
                    devBackendUrl,
                    *backendSupervisor,
                    std::chrono::milliseconds(devReadyTimeoutMs),
                    readinessError)) {
                std::cerr << "[mediasoup][dev-supervisor] backend readiness failed: "
                          << readinessError
                          << "\n";
                backendSupervisor->stop(stopTimeout);
                return 1;
            }
        }
        eds::server_new::mediasoup::signaling::MediasoupSignalingGateway gateway(
            app,
            kDefaultWsPort,
            allowDirectMediasoupDebug,
            debugMode);
        if (!gateway.start()) {
            std::cerr << "Failed to start Mediasoup signaling gateway.\n";
            if (backendSupervisor.has_value()) {
                backendSupervisor->stop(stopTimeout);
            }
            return 1;
        }

        std::cout << "[mediasoup] signaling gateway started on ws://0.0.0.0:" << kDefaultWsPort << '\n';
        if (debugMode) {
            std::cout << "[mediasoup] debug mode is enabled: packet relay tracing and traffic stats are active.\n";
        } else {
            std::cout << "[mediasoup] debug mode is disabled.\n";
        }
        if (allowDirectMediasoupDebug) {
            std::cout << "[mediasoup] direct command mode is enabled for tests only. "
                         "Target architecture should use media transport through feature orchestration.\n";
        } else {
            std::cout << "[mediasoup] direct command mode is disabled. "
                         "Use feature-level orchestration to access media transport.\n";
        }
        if (backendSupervisor.has_value()) {
            std::cout << "[mediasoup][dev-supervisor] child backend monitoring is active.\n";
        }

        std::atomic<bool> monitorStopRequested = false;
        std::atomic<bool> childCrashDetected = false;
        std::mutex childCrashMutex;
        std::string childCrashReason;
        std::string childCrashOutput;
        std::thread childMonitorThread;

        if (backendSupervisor.has_value()) {
            childMonitorThread = std::thread([&] {
                while (!monitorStopRequested.load(std::memory_order_acquire)) {
                    const auto childExit = backendSupervisor->pollUnexpectedExit();
                    if (childExit.has_value()) {
                        {
                            std::lock_guard<std::mutex> lock(childCrashMutex);
                            childCrashReason = childExit->reason;
                            childCrashOutput = childExit->recentOutput;
                        }
                        childCrashDetected.store(true, std::memory_order_release);
                        gateway.stop();
                        return;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                }
            });
        }

        gateway.wait();
        monitorStopRequested.store(true, std::memory_order_release);
        if (childMonitorThread.joinable()) {
            childMonitorThread.join();
        }
        if (backendSupervisor.has_value()) {
            backendSupervisor->stop(stopTimeout);
        }
        if (childCrashDetected.load(std::memory_order_acquire)) {
            std::lock_guard<std::mutex> lock(childCrashMutex);
            std::cerr << "[mediasoup][dev-supervisor] backend child process crashed: "
                      << childCrashReason
                      << "\n";
            if (!childCrashOutput.empty()) {
                std::cerr << "[mediasoup][dev-supervisor] child output before crash:\n"
                          << childCrashOutput;
            }
            return 1;
        }
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
