#pragma once

#include <memory>
#include <vector>
#include <thread>

namespace Sys {
    namespace Network {

        class cNetIoContext {
        public:
            cNetIoContext();
            ~cNetIoContext();

            // initialize internal structures; must be called before fnStart
            bool fnInit(unsigned int uThreadCount = 0);

            // start background threads running the io_context
            bool fnStart();

            // stop and join threads
            void fnStop();

            // accessor to underlying io_context placeholder
            // The original implementation used boost::asio::io_context.
            // To remove the Boost dependency we expose a lightweight stub
            // that satisfies the interface needs of the rest of the code.
            struct sIoStub {};
            sIoStub& fnIo() noexcept { return m_oIoContext; }

        private:
            // non-copyable
            cNetIoContext(const cNetIoContext&) = delete;
            cNetIoContext& operator=(const cNetIoContext&) = delete;

            sIoStub m_oIoContext;
            std::vector<std::thread> m_vThreads;
            unsigned int m_uThreadCountHint; // 0 -> hardware_concurrency
            bool m_bRunning;
        };

    } // namespace Network
} // namespace Sys
