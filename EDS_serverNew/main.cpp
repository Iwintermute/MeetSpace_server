#include "App/ApplicationCore.h"
#include "Auth/runtime/AuthServices.h"
#include "Bridge/Mediasoup/runtime/MediasoupBackendReadinessProbe.h"
#include "Bridge/Mediasoup/runtime/MediasoupBackendSupervisor.h"
#include "Bridge/Mediasoup/runtime/MediasoupDebugConfig.h"
#include "Bridge/Mediasoup/signaling/MediasoupSignalingGateway.h"
#include "infrastructure/control_plane/runtime/ControlPlaneServices.h"

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#endif

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

    std::string readFirstEnvVar(std::initializer_list<const char*> names) {
        for (const auto* name : names) {
            if (name == nullptr || name[0] == '\0') {
                continue;
            }
            auto value = readEnvVar(name);
            if (!value.empty()) {
                return value;
            }
        }
        return {};
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

    bool setEnvAliases(
        std::initializer_list<const char*> names,
        const std::string& value,
        std::string& error) {
        for (const auto* name : names) {
            if (name == nullptr || name[0] == '\0') {
                continue;
            }
            if (!setEnvVar(name, value, error)) {
                return false;
            }
        }
        error.clear();
        return true;
    }

    std::string trimCopy(const std::string& value) {
        const auto begin = value.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos) {
            return {};
        }
        const auto end = value.find_last_not_of(" \t\r\n");
        return value.substr(begin, end - begin + 1);
    }

    std::string stripWrappingQuotes(const std::string& value) {
        if (value.size() < 2) {
            return value;
        }
        const auto first = value.front();
        const auto last = value.back();
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            return value.substr(1, value.size() - 2);
        }
        return value;
    }

    std::optional<std::filesystem::path> resolveExecutableDirectory() {
#ifdef _WIN32
        std::vector<char> buffer(32768, '\0');
        const auto length = GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0 || length >= buffer.size()) {
            return std::nullopt;
        }
        std::filesystem::path executablePath(std::string(buffer.data(), length));
        return executablePath.parent_path();
#else
        return std::nullopt;
#endif
    }

    std::vector<std::filesystem::path> serverEnvCandidatePaths() {
        std::vector<std::filesystem::path> candidates;
        std::unordered_set<std::string> seen;

        const auto addCandidate = [&](const std::filesystem::path& candidate) {
            const auto normalized = candidate.lexically_normal();
            const auto key = normalized.string();
            if (key.empty()) {
                return;
            }
            if (seen.insert(key).second) {
                candidates.push_back(normalized);
            }
        };

        try {
            const auto cwd = std::filesystem::current_path();
            addCandidate(cwd / "server.env");
            addCandidate(cwd / "app" / "server.env");
            addCandidate(cwd / "apps" / "server.env");
        }
        catch (...) {
        }

        const auto exeDir = resolveExecutableDirectory();
        if (exeDir.has_value()) {
            addCandidate(*exeDir / "server.env");
            addCandidate(*exeDir / ".." / ".." / ".." / "server.env");
            addCandidate(*exeDir / ".." / ".." / ".." / "app" / "server.env");
            addCandidate(*exeDir / ".." / ".." / ".." / "apps" / "server.env");
        }

        return candidates;
    }

    const std::unordered_set<std::string>& allowedServerEnvKeys() {
        static const std::unordered_set<std::string> keys = {
            "MEETSPACE_POSTGRES_CONNINFO",
            "EDUSPACE_POSTGRES_CONNINFO",
            "POSTGRES_CONNINFO",
            "MEETSPACE_POSTGRES_POOL_SIZE",
            "EDUSPACE_POSTGRES_POOL_SIZE",
            "MEETSPACE_SUPABASE_URL",
            "MEETSPACE_SUPABASE_ANON_KEY",
            "SUPABASE_URL",
            "SUPABASE_ANON_KEY",
            "MEETSPACE_MEDIASOUP_BACKEND_URL",
            "MEETSPACE_MEDIASOUP_BACKEND_CMD",
            "EDUSPACE_MEDIASOUP_BACKEND_URL",
            "EDUSPACE_MEDIASOUP_BACKEND_CMD",
            "MEETSPACE_ALLOW_DEV_AUTH_TOKENS",
            "EDUSPACE_ALLOW_DEV_AUTH_TOKENS",
            "MEETSPACE_SIGNALING_PORT",
            "EDUSPACE_SIGNALING_PORT",
            "MEETSPACE_SIGNALING_TLS_ENABLED",
            "EDUSPACE_SIGNALING_TLS_ENABLED",
            "MEETSPACE_SIGNALING_TLS_CERT_FILE",
            "EDUSPACE_SIGNALING_TLS_CERT_FILE",
            "MEETSPACE_SIGNALING_TLS_KEY_FILE",
            "EDUSPACE_SIGNALING_TLS_KEY_FILE",
            "MEETSPACE_SIGNALING_TLS_CA_FILE",
            "EDUSPACE_SIGNALING_TLS_CA_FILE",
            "MEETSPACE_SIGNALING_TLS_DH_FILE",
            "EDUSPACE_SIGNALING_TLS_DH_FILE",
            "MEETSPACE_SIGNALING_TLS_REQUIRE_CLIENT_CERT",
            "EDUSPACE_SIGNALING_TLS_REQUIRE_CLIENT_CERT",
            "MEDIASOUP_BACKEND_HOST",
            "MEDIASOUP_BACKEND_PORT",
            "MEDIASOUP_BACKEND_PATH",
            "MEDIASOUP_BACKEND_TLS_ENABLED",
            "MEDIASOUP_BACKEND_TLS_CERT_FILE",
            "MEDIASOUP_BACKEND_TLS_KEY_FILE",
            "MEDIASOUP_BACKEND_TLS_CA_FILE",
            "MEDIASOUP_BACKEND_TLS_REQUIRE_CLIENT_CERT",
            "MEDIASOUP_ANNOUNCED_IP",
            "MEDIASOUP_RTC_LISTEN_IP",
            "MEDIASOUP_RTC_MIN_PORT",
            "MEDIASOUP_RTC_MAX_PORT",
            "MEETSPACE_MEDIASOUP_BACKEND_TLS_CA_FILE",
            "EDUSPACE_MEDIASOUP_BACKEND_TLS_CA_FILE",
            "MEETSPACE_MEDIASOUP_BACKEND_TLS_SERVER_NAME",
            "EDUSPACE_MEDIASOUP_BACKEND_TLS_SERVER_NAME",
            "MEETSPACE_MEDIASOUP_BACKEND_TLS_INSECURE_SKIP_VERIFY",
            "EDUSPACE_MEDIASOUP_BACKEND_TLS_INSECURE_SKIP_VERIFY",
            "MEETSPACE_MEDIA_POLICY_MAX_IDENTIFIER_LENGTH",
            "EDUSPACE_MEDIA_POLICY_MAX_IDENTIFIER_LENGTH",
            "MEETSPACE_MEDIA_POLICY_MAX_SDP_BYTES",
            "EDUSPACE_MEDIA_POLICY_MAX_SDP_BYTES",
            "MEETSPACE_MEDIA_POLICY_MAX_CANDIDATE_BYTES",
            "EDUSPACE_MEDIA_POLICY_MAX_CANDIDATE_BYTES",
            "MEETSPACE_MEDIA_POLICY_MAX_JSON_PAYLOAD_BYTES",
            "EDUSPACE_MEDIA_POLICY_MAX_JSON_PAYLOAD_BYTES",
            "MEETSPACE_MEDIA_POLICY_MAX_ACTIONS_PER_WINDOW",
            "EDUSPACE_MEDIA_POLICY_MAX_ACTIONS_PER_WINDOW",
            "MEETSPACE_MEDIA_POLICY_ACTION_WINDOW_SECONDS",
            "EDUSPACE_MEDIA_POLICY_ACTION_WINDOW_SECONDS",
            "MEETSPACE_MEDIA_POLICY_BACKEND_CONNECT_TIMEOUT_MS",
            "EDUSPACE_MEDIA_POLICY_BACKEND_CONNECT_TIMEOUT_MS",
            "MEETSPACE_MEDIA_POLICY_BACKEND_OPERATION_TIMEOUT_MS",
            "EDUSPACE_MEDIA_POLICY_BACKEND_OPERATION_TIMEOUT_MS",
            "MEETSPACE_MEDIA_POLICY_BACKEND_MAX_RETRIES",
            "EDUSPACE_MEDIA_POLICY_BACKEND_MAX_RETRIES",
            "MEETSPACE_MEDIA_POLICY_ALLOW_TEST_RTP_INJECTION",
            "EDUSPACE_MEDIA_POLICY_ALLOW_TEST_RTP_INJECTION",
            "MEETSPACE_MEDIA_POLICY_ENFORCE_TRACK_TYPE",
            "EDUSPACE_MEDIA_POLICY_ENFORCE_TRACK_TYPE",
            "MEETSPACE_MEDIA_POLICY_ENFORCE_KIND",
            "EDUSPACE_MEDIA_POLICY_ENFORCE_KIND"
        };
        return keys;
    }

    bool parseServerEnvFile(
        const std::filesystem::path& filePath,
        std::vector<std::pair<std::string, std::string>>& entries,
        std::string& error) {
        error.clear();
        entries.clear();

        std::ifstream input(filePath);
        if (!input.is_open()) {
            error = std::string("Failed to open file: ") + filePath.string();
            return false;
        }

        std::string line;
        std::size_t lineNumber = 0;
        while (std::getline(input, line)) {
            ++lineNumber;
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            auto trimmed = trimCopy(line);
            if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') {
                continue;
            }
            if (trimmed.rfind("export ", 0) == 0) {
                trimmed = trimCopy(trimmed.substr(7));
            }

            const auto equalPos = trimmed.find('=');
            if (equalPos == std::string::npos) {
                continue;
            }

            auto key = trimCopy(trimmed.substr(0, equalPos));
            if (key.empty()) {
                continue;
            }

            auto value = trimCopy(trimmed.substr(equalPos + 1));
            value = stripWrappingQuotes(value);
            entries.emplace_back(std::move(key), std::move(value));
        }

        if (!input.good() && !input.eof()) {
            error = std::string("Failed to read file: ") + filePath.string() + " at line " + std::to_string(lineNumber);
            entries.clear();
            return false;
        }

        return true;
    }

    void bootstrapEnvironmentFromServerEnvFile() {
        const auto& whitelist = allowedServerEnvKeys();
        std::string envError;

        for (const auto& candidate : serverEnvCandidatePaths()) {
            std::error_code fsError;
            if (!std::filesystem::exists(candidate, fsError) || fsError) {
                continue;
            }

            std::vector<std::pair<std::string, std::string>> entries;
            std::string parseError;
            if (!parseServerEnvFile(candidate, entries, parseError)) {
                std::cerr << "[bootstrap] failed to parse " << candidate.string() << ": " << parseError << "\n";
                continue;
            }

            int appliedCount = 0;
            for (const auto& [key, value] : entries) {
                if (value.empty()) {
                    continue;
                }
                if (whitelist.find(key) == whitelist.end()) {
                    continue;
                }

                if (!setEnvVar(key.c_str(), value, envError)) {
                    std::cerr << "[bootstrap] failed to set env '" << key
                        << "' from " << candidate.string() << ": " << envError << "\n";
                    continue;
                }
                ++appliedCount;
            }

            std::cout << "[bootstrap] loaded runtime env from " << candidate.string()
                << " (applied " << appliedCount << " value(s)).\n";
            return;
        }
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

    bool parseUnsignedShortOption(const std::string& value, unsigned short& result) {
        int parsed = 0;
        if (!parsePositiveIntOption(value, parsed)) {
            return false;
        }
        if (parsed > 65535) {
            return false;
        }
        result = static_cast<unsigned short>(parsed);
        return true;
    }

    bool parseBooleanOption(std::string value, bool& result) {
        if (value.empty()) {
            return false;
        }

        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
            });

        if (value == "1" || value == "true" || value == "yes" || value == "on") {
            result = true;
            return true;
        }
        if (value == "0" || value == "false" || value == "no" || value == "off") {
            result = false;
            return true;
        }
        return false;
    }

    bool readFirstBooleanEnvVar(
        std::initializer_list<const char*> names,
        bool fallbackValue = false) {
        for (const auto* name : names) {
            if (name == nullptr || name[0] == '\0') {
                continue;
            }

            auto value = readEnvVar(name);
            if (value.empty()) {
                continue;
            }

            bool parsed = fallbackValue;
            if (parseBooleanOption(value, parsed)) {
                return parsed;
            }
        }

        return fallbackValue;
    }

    bool bootstrapServerDependencies(std::string& error) {
        error.clear();
        std::string conninfo = readFirstEnvVar(
            { "MEETSPACE_POSTGRES_CONNINFO", "EDUSPACE_POSTGRES_CONNINFO", "POSTGRES_CONNINFO" });
        if (conninfo.empty()) {
            conninfo = kDefaultPostgresConninfo;
        }
        if (conninfo.empty()) {
            error =
                "Postgres conninfo is not configured. Set MEETSPACE_POSTGRES_CONNINFO, "
                "EDUSPACE_POSTGRES_CONNINFO, or POSTGRES_CONNINFO.";
            return false;
        }
        std::string supabaseUrl = readFirstEnvVar({ "MEETSPACE_SUPABASE_URL", "SUPABASE_URL" });
        std::string supabaseAnonKey =
            readFirstEnvVar({ "MEETSPACE_SUPABASE_ANON_KEY", "SUPABASE_ANON_KEY" });
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
        if (!setEnvAliases(
            { "MEETSPACE_POSTGRES_CONNINFO", "EDUSPACE_POSTGRES_CONNINFO", "POSTGRES_CONNINFO" },
            conninfo,
            envError)) {
            error = envError;
            return false;
        }
        if (!setEnvAliases(
            { "MEETSPACE_SUPABASE_URL", "SUPABASE_URL" },
            supabaseUrl,
            envError)) {
            error = envError;
            return false;
        }
        if (!setEnvAliases(
            { "MEETSPACE_SUPABASE_ANON_KEY", "SUPABASE_ANON_KEY" },
            supabaseAnonKey,
            envError)) {
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
    bool allowDevAuthTokens = false;
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
        if (arg == "--allow-dev-auth-tokens") {
            allowDevAuthTokens = true;
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
    if (!runServer && allowDevAuthTokens) {
        std::cerr << "--allow-dev-auth-tokens requires --server mode.\n";
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
        bootstrapEnvironmentFromServerEnvFile();
        eds::server_new::auth::AuthServices::setAllowDevAuthTokens(allowDevAuthTokens);
        if (allowDevAuthTokens) {
            std::string setEnvError;
            if (!setEnvAliases(
                { "MEETSPACE_ALLOW_DEV_AUTH_TOKENS", "EDUSPACE_ALLOW_DEV_AUTH_TOKENS" },
                "1",
                setEnvError)) {
                std::cerr << "[auth][bootstrap] " << setEnvError << "\n";
                return 1;
            }
        }
        std::string bootstrapError;
        if (!bootstrapServerDependencies(bootstrapError)) {
            std::cerr << "[bootstrap] " << bootstrapError << "\n";
            return 1;
        }

        if (devBackendUrl.empty()) {
            devBackendUrl = readFirstEnvVar(
                { "MEETSPACE_MEDIASOUP_BACKEND_URL", "EDUSPACE_MEDIASOUP_BACKEND_URL" });
        }
        if (!devBackendUrl.empty()) {
            std::string setEnvError;
            if (!setEnvAliases(
                { "MEETSPACE_MEDIASOUP_BACKEND_URL", "EDUSPACE_MEDIASOUP_BACKEND_URL" },
                devBackendUrl,
                setEnvError)) {
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
                devBackendCommand = readFirstEnvVar(
                    { "MEETSPACE_MEDIASOUP_BACKEND_CMD", "EDUSPACE_MEDIASOUP_BACKEND_CMD" });
            }
            if (devBackendCommand.empty()) {
                std::cerr << "[mediasoup][dev-supervisor] backend command is not configured. "
                    "Use --mediasoup-backend-cmd or MEETSPACE_MEDIASOUP_BACKEND_CMD.\n";
                return 1;
            }

            if (devBackendUrl.empty()) {
                devBackendUrl = readFirstEnvVar(
                    { "MEETSPACE_MEDIASOUP_BACKEND_URL", "EDUSPACE_MEDIASOUP_BACKEND_URL" });
            }
            if (devBackendUrl.empty()) {
                std::cerr << "[mediasoup][dev-supervisor] backend URL is not configured. "
                    "Use --mediasoup-backend-url or MEETSPACE_MEDIASOUP_BACKEND_URL.\n";
                return 1;
            }

            std::string setEnvError;
            if (!setEnvAliases(
                { "MEETSPACE_MEDIASOUP_BACKEND_URL", "EDUSPACE_MEDIASOUP_BACKEND_URL" },
                devBackendUrl,
                setEnvError)) {
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

        unsigned short signalingPort = kDefaultWsPort;
        const auto signalingPortValue = readFirstEnvVar(
            { "MEETSPACE_SIGNALING_PORT", "EDUSPACE_SIGNALING_PORT" });
        if (!signalingPortValue.empty()
            && !parseUnsignedShortOption(signalingPortValue, signalingPort)) {
            std::cerr << "[signaling] invalid signaling port in env. "
                "Use MEETSPACE_SIGNALING_PORT/EDUSPACE_SIGNALING_PORT with value 1..65535.\n";
            if (backendSupervisor.has_value()) {
                backendSupervisor->stop(stopTimeout);
            }
            return 1;
        }

        const auto signalingTlsCertFile = readFirstEnvVar(
            { "MEETSPACE_SIGNALING_TLS_CERT_FILE", "EDUSPACE_SIGNALING_TLS_CERT_FILE" });
        const auto signalingTlsKeyFile = readFirstEnvVar(
            { "MEETSPACE_SIGNALING_TLS_KEY_FILE", "EDUSPACE_SIGNALING_TLS_KEY_FILE" });
        const auto signalingTlsCaFile = readFirstEnvVar(
            { "MEETSPACE_SIGNALING_TLS_CA_FILE", "EDUSPACE_SIGNALING_TLS_CA_FILE" });
        const auto signalingTlsDhFile = readFirstEnvVar(
            { "MEETSPACE_SIGNALING_TLS_DH_FILE", "EDUSPACE_SIGNALING_TLS_DH_FILE" });
        const bool signalingTlsEnabled = readFirstBooleanEnvVar(
            { "MEETSPACE_SIGNALING_TLS_ENABLED", "EDUSPACE_SIGNALING_TLS_ENABLED" },
            !signalingTlsCertFile.empty() && !signalingTlsKeyFile.empty());
        const bool signalingTlsRequireClientCert = readFirstBooleanEnvVar(
            { "MEETSPACE_SIGNALING_TLS_REQUIRE_CLIENT_CERT", "EDUSPACE_SIGNALING_TLS_REQUIRE_CLIENT_CERT" },
            false);

        eds::server_new::mediasoup::transport::WebSocketTlsOptions signalingTlsOptions;
        signalingTlsOptions.enabled = signalingTlsEnabled;
        signalingTlsOptions.certificateChainFile = signalingTlsCertFile;
        signalingTlsOptions.privateKeyFile = signalingTlsKeyFile;
        signalingTlsOptions.caCertificateFile = signalingTlsCaFile;
        signalingTlsOptions.dhParamsFile = signalingTlsDhFile;
        signalingTlsOptions.requireClientCertificate = signalingTlsRequireClientCert;

        if (signalingTlsOptions.enabled
            && (signalingTlsOptions.certificateChainFile.empty()
                || signalingTlsOptions.privateKeyFile.empty())) {
            std::cerr << "[signaling] TLS is enabled but certificate or private key file is not configured.\n";
            if (backendSupervisor.has_value()) {
                backendSupervisor->stop(stopTimeout);
            }
            return 1;
        }

        if (signalingTlsOptions.enabled
            && signalingTlsOptions.requireClientCertificate
            && signalingTlsOptions.caCertificateFile.empty()) {
            std::cerr << "[signaling] TLS client certificate verification requires CA file.\n";
            if (backendSupervisor.has_value()) {
                backendSupervisor->stop(stopTimeout);
            }
            return 1;
        }

        eds::server_new::mediasoup::signaling::MediasoupSignalingGateway gateway(
            app,
            signalingPort,
            allowDirectMediasoupDebug,
            debugMode,
            signalingTlsOptions);

        if (!gateway.start()) {
            std::cerr << "Failed to start Mediasoup signaling gateway.\n";
            if (backendSupervisor.has_value()) {
                backendSupervisor->stop(stopTimeout);
            }
            return 1;
        }

        std::cout << "[mediasoup] signaling gateway started on "
            << (signalingTlsOptions.enabled ? "wss://0.0.0.0:" : "ws://0.0.0.0:")
            << signalingPort
            << '\n';
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