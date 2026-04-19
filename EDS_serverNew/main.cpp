#include "App/ApplicationCore.h"
#include "Auth/runtime/AuthServices.h"
#include "Bridge/Mediasoup/runtime/MediasoupBackendReadinessProbe.h"
#include "Bridge/Mediasoup/runtime/MediasoupBackendSupervisor.h"
#include "Bridge/Mediasoup/runtime/MediasoupDebugConfig.h"
#include "Bridge/Mediasoup/signaling/MediasoupSignalingGateway.h"
#include "infrastructure/control_plane/runtime/ControlPlaneServices.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace {
    constexpr const char* kDefaultSupabaseUrl = "https://mtbbcaykjomycovrxdya.supabase.co";
    constexpr const char* kDefaultSupabaseAnonKey =
        "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6Im10YmJjYXlram9teWNvdnJ4ZHlhIiwicm9sZSI6ImFub24iLCJpYXQiOjE3NzQ5MDkyODUsImV4cCI6MjA5MDQ4NTI4NX0.AKhEpGPBoiLDfUqAu1-MUgvDDrYlw_M0N_wHdXS9Cx4";
    constexpr const char* kDefaultPostgresConninfo =
        "postgresql://postgres:No_exclus1vee@db.mtbbcaykjomycovrxdya.supabase.co:5432/postgres";
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
        }
        catch (...) {
            return false;
        }
    }

    bool bootstrapServerDependencies(std::string& error) {
        error.clear();

        std::string conninfo = readEnvVar("EDUSPACE_POSTGRES_CONNINFO");
        if (conninfo.empty()) {
            conninfo = readEnvVar("POSTGRES_CONNINFO");
        }
        if (conninfo.empty()) {
            conninfo = kDefaultPostgresConninfo;
        }
        if (conninfo.empty()) {
            error =
                "Postgres conninfo is not configured. Set EDUSPACE_POSTGRES_CONNINFO or POSTGRES_CONNINFO.";
            return false;
        }
        std::string supabaseUrl = readEnvVar("SUPABASE_URL");
        std::string supabaseAnonKey = readEnvVar("SUPABASE_ANON_KEY");
        if (supabaseUrl.empty()) {
            supabaseUrl = kDefaultSupabaseUrl;
        }
        if (supabaseAnonKey.empty()) {
            supabaseAnonKey = kDefaultSupabaseAnonKey;
        }
        if (supabaseUrl.empty() || supabaseAnonKey.empty()) {
            error = "SUPABASE_URL and SUPABASE_ANON_KEY must be configured.";
            return false;
        }

        std::string envError;
        if (!setEnvVar("EDUSPACE_POSTGRES_CONNINFO", conninfo, envError)) {
            error = envError;
            return false;
        }
        if (!setEnvVar("POSTGRES_CONNINFO", conninfo, envError)) {
            error = envError;
            return false;
        }
        if (!setEnvVar("SUPABASE_URL", supabaseUrl, envError)) {
            error = envError;
            return false;
        }
        if (!setEnvVar("SUPABASE_ANON_KEY", supabaseAnonKey, envError)) {
            error = envError;
            return false;
        }

        if (!eds::server_new::control_plane::ControlPlaneServices::configure(conninfo, error)) {
            return false;
        }

        eds::server_new::auth::AuthServices::configure(supabaseUrl, supabaseAnonKey);
        return true;
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

        if (arg == "-d") {
            runServer = true;
            debugMode = true;
            devAutostartBackend = true;
            devBackendCommand.clear();
            devBackendUrl.clear();
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
        std::string bootstrapError;
        if (!bootstrapServerDependencies(bootstrapError)) {
            std::cerr << "[bootstrap] " << bootstrapError << "\n";
            return 1;
        }

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
        std::cout << (debugMode
            ? "[mediasoup] debug mode is enabled.\n"
            : "[mediasoup] debug mode is disabled.\n");

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

    std::cout << "[mediasoup] standalone demo dispatch is removed. Use --server mode.\n";
    return app.start() ? 0 : 1;
}