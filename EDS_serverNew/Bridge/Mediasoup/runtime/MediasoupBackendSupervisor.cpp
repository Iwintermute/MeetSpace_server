#include "Bridge/Mediasoup/runtime/MediasoupBackendSupervisor.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

namespace eds::server_new::mediasoup::runtime {
namespace {

constexpr std::size_t kMaxCapturedOutputLines = 200;

#ifdef _WIN32

std::string formatWindowsError(DWORD code) {
    LPSTR buffer = nullptr;
    const auto length = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&buffer),
        0,
        nullptr);
    std::string message;
    if (length != 0 && buffer != nullptr) {
        message.assign(buffer, length);
    } else {
        message = "Unknown Win32 error";
    }
    if (buffer != nullptr) {
        LocalFree(buffer);
    }
    while (!message.empty() && (message.back() == '\r' || message.back() == '\n')) {
        message.pop_back();
    }
    return message;
}

void closeHandleIfNeeded(HANDLE& handle) {
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
        CloseHandle(handle);
        handle = nullptr;
    }
}

#endif

} // namespace

struct MediasoupBackendSupervisor::Impl {
    mutable std::mutex stateMutex;
    mutable std::mutex outputMutex;
    std::deque<std::string> outputLines;
    bool running = false;
    bool exitWasReported = false;
    int lastExitCode = 0;

#ifdef _WIN32
    PROCESS_INFORMATION processInfo{};
    HANDLE stdoutRead = nullptr;
    HANDLE stdoutWrite = nullptr;
    HANDLE stderrRead = nullptr;
    HANDLE stderrWrite = nullptr;
    std::thread stdoutReaderThread;
    std::thread stderrReaderThread;
    std::atomic<bool> stopReaders = false;
#endif
};

MediasoupBackendSupervisor::MediasoupBackendSupervisor()
    : impl_(new Impl()) {
}

MediasoupBackendSupervisor::~MediasoupBackendSupervisor() {
    stop(std::chrono::milliseconds(3000));
    delete impl_;
    impl_ = nullptr;
}

bool MediasoupBackendSupervisor::start(const std::string& commandLine, std::string& error) {
    error.clear();
    if (commandLine.empty()) {
        error = "Mediasoup backend command is empty.";
        return false;
    }
    if (impl_ == nullptr) {
        error = "Mediasoup backend supervisor is not initialized.";
        return false;
    }

#ifndef _WIN32
    error = "Mediasoup dev backend supervisor is currently implemented only for Windows.";
    return false;
#else
    std::lock_guard<std::mutex> lock(impl_->stateMutex);
    if (impl_->running) {
        error = "Mediasoup backend process is already running.";
        return false;
    }
    resetNoLock();

    SECURITY_ATTRIBUTES securityAttributes{};
    securityAttributes.nLength = sizeof(securityAttributes);
    securityAttributes.bInheritHandle = TRUE;

    if (!CreatePipe(&impl_->stdoutRead, &impl_->stdoutWrite, &securityAttributes, 0)) {
        error = "Failed to create mediasoup stdout pipe: " + formatWindowsError(GetLastError());
        resetNoLock();
        return false;
    }
    if (!SetHandleInformation(impl_->stdoutRead, HANDLE_FLAG_INHERIT, 0)) {
        error = "Failed to configure mediasoup stdout pipe: " + formatWindowsError(GetLastError());
        resetNoLock();
        return false;
    }

    if (!CreatePipe(&impl_->stderrRead, &impl_->stderrWrite, &securityAttributes, 0)) {
        error = "Failed to create mediasoup stderr pipe: " + formatWindowsError(GetLastError());
        resetNoLock();
        return false;
    }
    if (!SetHandleInformation(impl_->stderrRead, HANDLE_FLAG_INHERIT, 0)) {
        error = "Failed to configure mediasoup stderr pipe: " + formatWindowsError(GetLastError());
        resetNoLock();
        return false;
    }

    STARTUPINFOA startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startupInfo.hStdOutput = impl_->stdoutWrite;
    startupInfo.hStdError = impl_->stderrWrite;

    std::string commandLineMutable = commandLine;
    const auto created = CreateProcessA(
        nullptr,
        commandLineMutable.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP,
        nullptr,
        nullptr,
        &startupInfo,
        &impl_->processInfo);
    if (!created) {
        error = "Failed to start mediasoup backend process: " + formatWindowsError(GetLastError());
        resetNoLock();
        return false;
    }

    closeHandleIfNeeded(impl_->stdoutWrite);
    closeHandleIfNeeded(impl_->stderrWrite);
    impl_->stopReaders.store(false, std::memory_order_release);

    const auto startReader = [this](HANDLE readHandle, const char* prefix) {
        return std::thread([this, readHandle, prefix] {
            std::array<char, 512> buffer{};
            std::string line;
            while (!impl_->stopReaders.load(std::memory_order_acquire)) {
                DWORD bytesRead = 0;
                const auto readResult = ReadFile(
                    readHandle,
                    buffer.data(),
                    static_cast<DWORD>(buffer.size()),
                    &bytesRead,
                    nullptr);
                if (!readResult || bytesRead == 0) {
                    break;
                }

                for (DWORD i = 0; i < bytesRead; ++i) {
                    const auto symbol = buffer[i];
                    if (symbol == '\r') {
                        continue;
                    }
                    if (symbol == '\n') {
                        if (!line.empty()) {
                            appendOutputLine(std::string(prefix) + line);
                            line.clear();
                        }
                        continue;
                    }
                    line.push_back(symbol);
                }
            }
            if (!line.empty()) {
                appendOutputLine(std::string(prefix) + line);
            }
        });
    };

    impl_->stdoutReaderThread = startReader(impl_->stdoutRead, "[child][stdout] ");
    impl_->stderrReaderThread = startReader(impl_->stderrRead, "[child][stderr] ");

    impl_->running = true;
    impl_->exitWasReported = false;
    impl_->lastExitCode = 0;
    appendOutputLine("[system] mediasoup backend process started.");
    return true;
#endif
}

void MediasoupBackendSupervisor::stop(std::chrono::milliseconds timeout) {
    if (impl_ == nullptr) {
        return;
    }

#ifndef _WIN32
    static_cast<void>(timeout);
    return;
#else
    HANDLE processHandle = nullptr;
    {
        std::lock_guard<std::mutex> lock(impl_->stateMutex);
        processHandle = impl_->processInfo.hProcess;
        if (processHandle != nullptr && processHandle != INVALID_HANDLE_VALUE) {
            const auto waitResult = WaitForSingleObject(
                processHandle,
                timeout.count() > 0 ? static_cast<DWORD>(timeout.count()) : 0);
            if (waitResult == WAIT_TIMEOUT) {
                appendOutputLine("[system] mediasoup backend stop timeout reached, terminating process.");
                TerminateProcess(processHandle, 1);
                WaitForSingleObject(processHandle, 5000);
            }

            DWORD exitCode = 0;
            if (GetExitCodeProcess(processHandle, &exitCode) && exitCode != STILL_ACTIVE) {
                impl_->lastExitCode = static_cast<int>(exitCode);
            }
        }

        impl_->running = false;
        impl_->exitWasReported = true;
        impl_->stopReaders.store(true, std::memory_order_release);
        closeHandleIfNeeded(impl_->stdoutRead);
        closeHandleIfNeeded(impl_->stderrRead);
        closeHandleIfNeeded(impl_->stdoutWrite);
        closeHandleIfNeeded(impl_->stderrWrite);
    }

    joinReaderThreadsNoLock();

    std::lock_guard<std::mutex> lock(impl_->stateMutex);
    closeHandleIfNeeded(impl_->processInfo.hThread);
    closeHandleIfNeeded(impl_->processInfo.hProcess);
#endif
}

std::optional<BackendProcessExitInfo> MediasoupBackendSupervisor::pollUnexpectedExit() {
    if (impl_ == nullptr) {
        return std::nullopt;
    }

#ifndef _WIN32
    return std::nullopt;
#else
    bool shouldReport = false;
    int exitCode = 0;
    std::string reason;

    {
        std::lock_guard<std::mutex> lock(impl_->stateMutex);
        if (!impl_->running || impl_->exitWasReported) {
            return std::nullopt;
        }
        if (impl_->processInfo.hProcess == nullptr || impl_->processInfo.hProcess == INVALID_HANDLE_VALUE) {
            impl_->running = false;
            impl_->exitWasReported = true;
            reason = "Mediasoup backend process handle is invalid.";
            shouldReport = true;
        } else {
            DWORD nativeExitCode = STILL_ACTIVE;
            if (!GetExitCodeProcess(impl_->processInfo.hProcess, &nativeExitCode)) {
                impl_->running = false;
                impl_->exitWasReported = true;
                reason = "Failed to read mediasoup backend exit code: " + formatWindowsError(GetLastError());
                shouldReport = true;
            } else if (nativeExitCode != STILL_ACTIVE) {
                impl_->running = false;
                impl_->lastExitCode = static_cast<int>(nativeExitCode);
                exitCode = impl_->lastExitCode;
                reason = "Mediasoup backend child process exited with code " + std::to_string(exitCode) + ".";
                shouldReport = true;
            }
        }

        if (shouldReport) {
            impl_->exitWasReported = true;
            impl_->stopReaders.store(true, std::memory_order_release);
            closeHandleIfNeeded(impl_->stdoutRead);
            closeHandleIfNeeded(impl_->stderrRead);
            closeHandleIfNeeded(impl_->stdoutWrite);
            closeHandleIfNeeded(impl_->stderrWrite);
        }
    }

    if (!shouldReport) {
        return std::nullopt;
    }

    joinReaderThreadsNoLock();

    {
        std::lock_guard<std::mutex> lock(impl_->stateMutex);
        closeHandleIfNeeded(impl_->processInfo.hThread);
        closeHandleIfNeeded(impl_->processInfo.hProcess);
    }

    BackendProcessExitInfo info;
    info.exitCode = exitCode;
    info.reason = std::move(reason);
    info.recentOutput = recentOutputSnapshot();
    return info;
#endif
}

std::string MediasoupBackendSupervisor::recentOutputSnapshot() const {
    if (impl_ == nullptr) {
        return {};
    }

    std::lock_guard<std::mutex> lock(impl_->outputMutex);
    std::string merged;
    for (const auto& line : impl_->outputLines) {
        merged += line;
        merged.push_back('\n');
    }
    return merged;
}

bool MediasoupBackendSupervisor::isRunning() const {
    if (impl_ == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(impl_->stateMutex);
    return impl_->running;
}

void MediasoupBackendSupervisor::resetNoLock() {
    if (impl_ == nullptr) {
        return;
    }

#ifdef _WIN32
    impl_->stopReaders.store(true, std::memory_order_release);
    closeHandleIfNeeded(impl_->stdoutRead);
    closeHandleIfNeeded(impl_->stderrRead);
    closeHandleIfNeeded(impl_->stdoutWrite);
    closeHandleIfNeeded(impl_->stderrWrite);
    closeHandleIfNeeded(impl_->processInfo.hThread);
    closeHandleIfNeeded(impl_->processInfo.hProcess);
#endif

    impl_->running = false;
    impl_->exitWasReported = false;
    impl_->lastExitCode = 0;
    {
        std::lock_guard<std::mutex> outputLock(impl_->outputMutex);
        impl_->outputLines.clear();
    }
}

void MediasoupBackendSupervisor::appendOutputLine(const std::string& line) {
    if (impl_ == nullptr || line.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(impl_->outputMutex);
    impl_->outputLines.push_back(line);
    while (impl_->outputLines.size() > kMaxCapturedOutputLines) {
        impl_->outputLines.pop_front();
    }
}

void MediasoupBackendSupervisor::joinReaderThreadsNoLock() {
    if (impl_ == nullptr) {
        return;
    }

#ifdef _WIN32
    if (impl_->stdoutReaderThread.joinable()) {
        impl_->stdoutReaderThread.join();
    }
    if (impl_->stderrReaderThread.joinable()) {
        impl_->stderrReaderThread.join();
    }
#endif
}

} // namespace eds::server_new::mediasoup::runtime
