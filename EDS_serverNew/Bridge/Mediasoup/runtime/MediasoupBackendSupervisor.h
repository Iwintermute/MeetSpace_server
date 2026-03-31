#pragma once

#include <chrono>
#include <optional>
#include <string>

namespace eds::server_new::mediasoup::runtime {

struct BackendProcessExitInfo {
    int exitCode = 0;
    std::string reason;
    std::string recentOutput;
};

class MediasoupBackendSupervisor {
public:
    MediasoupBackendSupervisor();
    ~MediasoupBackendSupervisor();

    MediasoupBackendSupervisor(const MediasoupBackendSupervisor&) = delete;
    MediasoupBackendSupervisor& operator=(const MediasoupBackendSupervisor&) = delete;

    bool start(const std::string& commandLine, std::string& error);
    void stop(std::chrono::milliseconds timeout);

    std::optional<BackendProcessExitInfo> pollUnexpectedExit();
    std::string recentOutputSnapshot() const;
    bool isRunning() const;

private:
    void resetNoLock();
    void appendOutputLine(const std::string& line);
    void joinReaderThreadsNoLock();

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace eds::server_new::mediasoup::runtime
