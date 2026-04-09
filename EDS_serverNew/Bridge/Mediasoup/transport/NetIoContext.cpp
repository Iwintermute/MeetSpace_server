#include "Bridge/Mediasoup/transport/NetIoContext.h"

#include <algorithm>
#include <iostream>

namespace eds::server_new::mediasoup::transport {

    NetIoContext::NetIoContext() = default;

    NetIoContext::~NetIoContext() {
        stop();
    }

    bool NetIoContext::init(unsigned int threadCount) {
        if (running_) {
            return false;
        }

        threadCountHint_ = threadCount;
        workGuard_ = std::make_unique<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>(
            ioContext_.get_executor());
        return true;
    }

    bool NetIoContext::start() {
        if (running_) {
            return true;
        }

        running_ = true;

        unsigned int threadCount = threadCountHint_;
        if (threadCount == 0) {
            const auto hw = std::thread::hardware_concurrency();
            threadCount = std::max(2u, hw);
        }

        try {
            threads_.reserve(threadCount);

            for (unsigned int i = 0; i < threadCount; ++i) {
                threads_.emplace_back([this]() {
                    try {
                        ioContext_.run();
                    }
                    catch (const std::exception& ex) {
                        std::cerr << "[NetIoContext] worker exception: " << ex.what() << '\n';
                    }
                    });
            }
        }
        catch (const std::exception& ex) {
            std::cerr << "[NetIoContext] failed to start threads: " << ex.what() << '\n';
            stop();
            return false;
        }

        return true;
    }

    void NetIoContext::stop() {
        if (workGuard_) {
            workGuard_.reset();
        }

        if (running_) {
            running_ = false;
            ioContext_.stop();
        }

        const auto currentThreadId = std::this_thread::get_id();

        for (auto& thread : threads_) {
            if (!thread.joinable()) {
                continue;
            }

            if (thread.get_id() == currentThreadId) {
                thread.detach();
                continue;
            }

            thread.join();
        }

        threads_.clear();
        ioContext_.restart();
    }

} // namespace eds::server_new::mediasoup::transport