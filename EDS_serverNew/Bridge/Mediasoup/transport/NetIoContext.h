#pragma once

#include <boost/asio/io_context.hpp>
#include <boost/asio/executor_work_guard.hpp>

#include <memory>
#include <thread>
#include <vector>

namespace eds::server_new::mediasoup::transport {

class NetIoContext {
public:
    NetIoContext();
    ~NetIoContext();

    bool init(unsigned int threadCount = 0);
    bool start();
    void stop();

    boost::asio::io_context& io() noexcept {
        return ioContext_;
    }

private:
    boost::asio::io_context ioContext_;
    std::unique_ptr<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> workGuard_;
    std::vector<std::thread> threads_;
    unsigned int threadCountHint_ = 0;
    bool running_ = false;
};

} // namespace eds::server_new::mediasoup::transport
