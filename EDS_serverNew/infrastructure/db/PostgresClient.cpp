#include "infrastructure/db/PostgresClient.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <thread>

namespace eds::server_new::infrastructure::db {
    namespace {
        constexpr std::size_t kMinPoolSize = 1;
        constexpr std::size_t kMaxPoolSize = 64;
        constexpr std::size_t kFallbackPoolSize = 8;
        constexpr const char* kPoolSizeEnvNamePrimary = "MEETSPACE_POSTGRES_POOL_SIZE";
        constexpr const char* kPoolSizeEnvNameCompat = "EDUSPACE_POSTGRES_POOL_SIZE";
        constexpr std::size_t kMinStatementAttempts = 3;
        constexpr std::size_t kReconnectAttemptsPerSlot = 2;
        constexpr auto kStatementRetryBackoff = std::chrono::milliseconds(35);

        std::size_t clampPoolSize(std::size_t value) {
            if (value < kMinPoolSize) {
                return kMinPoolSize;
            }
            if (value > kMaxPoolSize) {
                return kMaxPoolSize;
            }
            return value;
        }

        std::size_t readPositiveSizeFromEnv(const char* key) {
            if (key == nullptr || key[0] == '\0') {
                return 0;
            }
            const char* raw = std::getenv(key);
            if (raw == nullptr || raw[0] == '\0') {
                return 0;
            }

            try {
                const auto parsed = std::stoll(raw);
                if (parsed <= 0) {
                    return 0;
                }
                return clampPoolSize(static_cast<std::size_t>(parsed));
            }
            catch (...) {
                return 0;
            }
        }
        std::size_t readPoolSizeFromEnv() {
            const auto meetspaceValue = readPositiveSizeFromEnv(kPoolSizeEnvNamePrimary);
            if (meetspaceValue > 0) {
                return meetspaceValue;
            }
            return readPositiveSizeFromEnv(kPoolSizeEnvNameCompat);
        }

        std::size_t resolvePoolSize(std::size_t requestedPoolSize) {
            if (requestedPoolSize > 0) {
                return clampPoolSize(requestedPoolSize);
            }

            const auto envPoolSize = readPoolSizeFromEnv();
            if (envPoolSize > 0) {
                return envPoolSize;
            }

            const auto hw = static_cast<std::size_t>(std::thread::hardware_concurrency());
            if (hw == 0) {
                return kFallbackPoolSize;
            }
            const auto conservativeByHardware = hw >= 8 ? 8 : std::max<std::size_t>(2, hw);
            return clampPoolSize(std::min<std::size_t>(kFallbackPoolSize, conservativeByHardware));
        }

        std::string toLowerCopy(std::string value) {
            std::transform(
                value.begin(),
                value.end(),
                value.begin(),
                [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            return value;
        }


        bool reconnectConnectionLocked(
            PGconn*& connection,
            const std::string& conninfo,
            std::string& error) {
            error.clear();
            if (conninfo.empty()) {
                error = "Postgres conninfo is empty; reconnect is impossible.";
                return false;
            }

            if (connection != nullptr) {
                PQfinish(connection);
                connection = nullptr;
            }

            connection = PQconnectdb(conninfo.c_str());
            if (connection == nullptr) {
                error = "PQconnectdb returned nullptr while reconnecting a Postgres slot.";
                return false;
            }

            if (PQstatus(connection) != CONNECTION_OK) {
                const auto raw = PQerrorMessage(connection);
                error = raw != nullptr ? std::string(raw) : std::string("unknown libpq reconnect error");
                PQfinish(connection);
                connection = nullptr;
                return false;
            }

            return true;
        }

        bool ensureConnectionReadyLocked(
            PGconn*& connection,
            const std::string& conninfo,
            std::string& error) {
            error.clear();
            if (connection != nullptr && PQstatus(connection) == CONNECTION_OK) {
                return true;
            }
            return reconnectConnectionLocked(connection, conninfo, error);
        }

        bool isRecoverableConnectionError(
            PGconn* connection,
            ExecStatusType status,
            const std::string& errorText) {
            if (connection == nullptr || PQstatus(connection) != CONNECTION_OK) {
                return true;
            }

            if (status == PGRES_FATAL_ERROR || status == PGRES_BAD_RESPONSE) {
                return true;
            }
#ifdef PGRES_PIPELINE_ABORTED
            if (status == PGRES_PIPELINE_ABORTED) {
                return true;
            }
#endif

            const auto normalized = toLowerCopy(errorText);
            return normalized.find("server closed the connection unexpectedly") != std::string::npos ||
                normalized.find("terminating connection") != std::string::npos ||
                normalized.find("connection not open") != std::string::npos ||
                normalized.find("connection refused") != std::string::npos ||
                normalized.find("no connection to the server") != std::string::npos ||
                normalized.find("could not connect to server") != std::string::npos ||
                normalized.find("timeout expired") != std::string::npos;
        }
    } // namespace

    PostgresClient::PostgresClient() = default;

    PostgresClient::~PostgresClient() {
        std::vector<std::shared_ptr<ConnectionSlot>> staleSlots;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            staleSlots.swap(slots_);
            conninfo_.clear();
        }
        closeSlots(staleSlots);
    }

    bool PostgresClient::connect(const std::string& conninfo, std::string& error) {
        return connect(conninfo, error, 0);
    }

    bool PostgresClient::connect(const std::string& conninfo, std::string& error, std::size_t poolSize) {
        error.clear();
        const auto effectivePoolSize = resolvePoolSize(poolSize);
        std::vector<std::shared_ptr<ConnectionSlot>> newSlots;
        newSlots.reserve(effectivePoolSize);

        for (std::size_t i = 0; i < effectivePoolSize; ++i) {
            auto slot = std::make_shared<ConnectionSlot>();
            slot->connection = PQconnectdb(conninfo.c_str());

            if (slot->connection == nullptr) {
                std::ostringstream stream;
                stream << "PQconnectdb returned nullptr for slot #" << i << ".";
                error = stream.str();
                closeSlots(newSlots);
                return false;
            }

            if (PQstatus(slot->connection) != CONNECTION_OK) {
                const auto slotErrorRaw = PQerrorMessage(slot->connection);
                const std::string slotError = slotErrorRaw != nullptr
                    ? std::string(slotErrorRaw)
                    : std::string("unknown libpq error");
                std::ostringstream stream;
                stream << "Postgres connection failed for slot #" << i << ": "
                    << slotError;
                error = stream.str();
                PQfinish(slot->connection);
                slot->connection = nullptr;
                closeSlots(newSlots);
                return false;
            }

            newSlots.push_back(std::move(slot));
        }

        if (newSlots.empty()) {
            error = "Postgres connection pool failed to initialize: no live connections established.";
            return false;
        }

        std::vector<std::shared_ptr<ConnectionSlot>> staleSlots;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            staleSlots.swap(slots_);
            slots_ = std::move(newSlots);
            conninfo_ = conninfo;
            nextSlotIndex_.store(0, std::memory_order_relaxed);
        }
        closeSlots(staleSlots);
        return true;
    }

    bool PostgresClient::isConnected() const noexcept {
        std::vector<std::shared_ptr<ConnectionSlot>> slotsSnapshot;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            slotsSnapshot = slots_;
        }

        for (const auto& slot : slotsSnapshot) {
            if (slot == nullptr) {
                continue;
            }
            std::lock_guard<std::mutex> slotLock(slot->mutex);
            if (slot->connection == nullptr) {
                continue;
            }

            if (PQstatus(slot->connection) == CONNECTION_OK) {
                return true;
            }
        }
        return false;
    }

    std::size_t PostgresClient::poolSize() const noexcept {
        std::lock_guard<std::mutex> lock(stateMutex_);
        return slots_.size();
    }

    std::size_t PostgresClient::slotCountSnapshot() const noexcept {
        std::lock_guard<std::mutex> lock(stateMutex_);
        return slots_.size();
    }

    std::string PostgresClient::conninfoSnapshot() const {
        std::lock_guard<std::mutex> lock(stateMutex_);
        return conninfo_;
    }

    std::shared_ptr<PostgresClient::ConnectionSlot> PostgresClient::acquireSlot(std::string& error) const {
        error.clear();

        std::lock_guard<std::mutex> lock(stateMutex_);
        if (slots_.empty()) {
            error = "Postgres connection pool is not initialized.";
            return nullptr;
        }

        const auto index = nextSlotIndex_.fetch_add(1, std::memory_order_relaxed) % slots_.size();
        auto slot = slots_[index];
        if (slot == nullptr) {
            error = "Postgres connection pool slot is null.";
            return nullptr;
        }

        return slot;
    }

    void PostgresClient::closeSlots(std::vector<std::shared_ptr<ConnectionSlot>>& slots) noexcept {
        for (auto& slot : slots) {
            if (!slot) {
                continue;
            }

            std::lock_guard<std::mutex> lock(slot->mutex);
            if (slot->connection != nullptr) {
                PQfinish(slot->connection);
                slot->connection = nullptr;
            }
        }
        slots.clear();
    }

    PGresult* PostgresClient::execParamsLocked(
        PGconn* connection,
        const std::string& sql,
        const std::vector<std::string>& params) const {
        if (connection == nullptr) {
            return nullptr;
        }

        std::vector<const char*> values;
        values.reserve(params.size());

        for (const auto& value : params) {
            values.push_back(value.c_str());
        }

        return PQexecParams(
            connection,
            sql.c_str(),
            static_cast<int>(values.size()),
            nullptr,
            values.empty() ? nullptr : values.data(),
            nullptr,
            nullptr,
            0);
    }

    PGresult* PostgresClient::execParamsResilient(
        const std::string& sql,
        const std::vector<std::string>& params,
        bool allowCommandStatus,
        std::string& error) const {
        error.clear();

        const auto slotsCount = slotCountSnapshot();
        if (slotsCount == 0) {
            error = "Postgres connection pool is not initialized.";
            return nullptr;
        }

        const auto conninfo = conninfoSnapshot();
        if (conninfo.empty()) {
            error = "Postgres conninfo is not configured.";
            return nullptr;
        }

        const auto maxAttempts = std::max<std::size_t>(kMinStatementAttempts, slotsCount);
        std::string lastError;

        for (std::size_t attempt = 0; attempt < maxAttempts; ++attempt) {
            std::string acquireError;
            auto slot = acquireSlot(acquireError);
            if (!slot) {
                lastError = acquireError;
                continue;
            }

            std::lock_guard<std::mutex> lock(slot->mutex);

            bool slotReady = false;
            for (std::size_t reconnectAttempt = 0; reconnectAttempt < kReconnectAttemptsPerSlot; ++reconnectAttempt) {
                std::string readyError;
                if (ensureConnectionReadyLocked(slot->connection, conninfo, readyError)) {
                    slotReady = true;
                    break;
                }
                lastError = readyError.empty() ? std::string("Postgres slot reconnect failed.") : readyError;
                if (reconnectAttempt + 1 < kReconnectAttemptsPerSlot) {
                    std::this_thread::sleep_for(kStatementRetryBackoff);
                }
            }
            if (!slotReady) {
                continue;
            }

            PGresult* result = execParamsLocked(slot->connection, sql, params);
            if (result == nullptr) {
                lastError = "PQexecParams returned nullptr.";
                std::string reconnectError;
                if (!reconnectConnectionLocked(slot->connection, conninfo, reconnectError) && !reconnectError.empty()) {
                    lastError = reconnectError;
                }
                if (attempt + 1 < maxAttempts) {
                    std::this_thread::sleep_for(kStatementRetryBackoff);
                }
                continue;
            }

            const auto status = PQresultStatus(result);
            const bool statusOk = status == PGRES_TUPLES_OK ||
                (allowCommandStatus && status == PGRES_COMMAND_OK);
            if (statusOk) {
                return result;
            }

            const auto resultErrorRaw = PQresultErrorMessage(result);
            const std::string resultError = resultErrorRaw != nullptr
                ? std::string(resultErrorRaw)
                : std::string{};
            const bool retryAllowed = isRecoverableConnectionError(slot->connection, status, resultError) &&
                (attempt + 1 < maxAttempts);

            if (!retryAllowed) {
                error = resultError.empty() ? std::string("Postgres operation failed.") : resultError;
                PQclear(result);
                return nullptr;
            }

            PQclear(result);
            std::string reconnectError;
            if (!reconnectConnectionLocked(slot->connection, conninfo, reconnectError) && !reconnectError.empty()) {
                lastError = reconnectError;
            }
            else if (!resultError.empty()) {
                lastError = resultError;
            }
            else {
                lastError = "Postgres operation failed due to transient connection state.";
            }

            std::this_thread::sleep_for(kStatementRetryBackoff);
        }

        error = lastError.empty() ? std::string("Postgres operation failed after retry attempts.") : lastError;
        return nullptr;
    }

    std::optional<PostgresClient::json> PostgresClient::queryScalarJson(
        const std::string& sql,
        const std::vector<std::string>& params,
        std::string& error) const {
        error.clear();
        PGresult* result = execParamsResilient(sql, params, false, error);
        if (result == nullptr) {
            return std::nullopt;
        }

        if (PQntuples(result) == 0 || PQnfields(result) == 0) {
            PQclear(result);
            return std::nullopt;
        }

        const auto raw = PQgetvalue(result, 0, 0);
        json parsed = json::parse(raw != nullptr ? raw : "null", nullptr, false);
        PQclear(result);

        if (parsed.is_discarded()) {
            error = "Failed to parse JSON returned from database.";
            return std::nullopt;
        }

        return parsed;
    }

    std::vector<PostgresClient::json> PostgresClient::queryJsonRows(
        const std::string& sql,
        const std::vector<std::string>& params,
        std::string& error) const {
        error.clear();
        std::vector<json> rows;

        PGresult* result = execParamsResilient(sql, params, false, error);
        if (result == nullptr) {
            return rows;
        }

        const auto tupleCount = PQntuples(result);
        rows.reserve(static_cast<size_t>(tupleCount));
        for (int i = 0; i < tupleCount; ++i) {
            const auto raw = PQgetvalue(result, i, 0);
            json parsed = json::parse(raw != nullptr ? raw : "null", nullptr, false);
            if (parsed.is_discarded()) {
                rows.clear();
                error = "Failed to parse JSON row returned from database.";
                PQclear(result);
                return rows;
            }
            rows.push_back(std::move(parsed));
        }

        PQclear(result);
        return rows;
    }

    std::vector<std::string> PostgresClient::queryStringRows(
        const std::string& sql,
        const std::vector<std::string>& params,
        std::string& error) const {
        error.clear();
        std::vector<std::string> rows;

        PGresult* result = execParamsResilient(sql, params, false, error);
        if (result == nullptr) {
            return rows;
        }

        const auto tupleCount = PQntuples(result);
        rows.reserve(static_cast<size_t>(tupleCount));
        for (int i = 0; i < tupleCount; ++i) {
            const auto raw = PQgetvalue(result, i, 0);
            rows.emplace_back(raw != nullptr ? raw : "");
        }

        PQclear(result);
        return rows;
    }

    bool PostgresClient::execute(
        const std::string& sql,
        const std::vector<std::string>& params,
        std::string& error) const {
        error.clear();

        PGresult* result = execParamsResilient(sql, params, true, error);
        if (result == nullptr) {
            return false;
        }

        PQclear(result);
        return true;
    }

} // namespace eds::server_new::infrastructure::db
