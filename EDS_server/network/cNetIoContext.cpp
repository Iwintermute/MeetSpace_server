#include "cNetIoContext.h"
#include <algorithm>
#include <iostream>

using namespace Sys::Network;

cNetIoContext::cNetIoContext()
    : m_oIoContext(),
    m_pWorkGuard(nullptr),
    m_vThreads(),
    m_uThreadCountHint(0),
    m_bRunning(false)
{
}

cNetIoContext::~cNetIoContext()
{
    fnStop();
}

bool cNetIoContext::fnInit(unsigned int uThreadCount)
{
    if (m_bRunning) return false;
    m_uThreadCountHint = uThreadCount;
    // create work guard (executor_work_guard keeps io_context alive)
    m_pWorkGuard = std::make_unique<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>(m_oIoContext.get_executor());
    return true;
}

bool cNetIoContext::fnStart()
{
    if (m_bRunning) return true;
    m_bRunning = true;

    unsigned int uThreads = m_uThreadCountHint;
    if (uThreads == 0) {
        unsigned int uHw = std::thread::hardware_concurrency();
        uThreads = (uHw == 0 ? 2u : uHw);
    }

    // Spawn threads; we keep them joinable in m_vThreads
    try {
        m_vThreads.reserve(uThreads);
        for (unsigned int i = 0; i < uThreads; ++i) {
            m_vThreads.emplace_back([this, i]() {
                try {
                    // run will return when work guard destroyed or stop() called
                    m_oIoContext.run();
                }
                catch (const std::exception& e) {
                    std::cerr << "[cNetIoContext] thread exception: " << e.what() << '\n';
                }
                });
        }
    }
    catch (const std::exception& e) {
        std::cerr << "[cNetIoContext] fnStart: failed to spawn threads: " << e.what() << '\n';
        fnStop();
        return false;
    }

    return true;
}

void cNetIoContext::fnStop()
{
    if (!m_bRunning) {
        // still ensure resources freed
        if (m_pWorkGuard) m_pWorkGuard.reset();
        return;
    }

    m_bRunning = false;

    // release the work guard so run() can complete
    if (m_pWorkGuard) m_pWorkGuard.reset();

    // request stop on io_context
    try {
        m_oIoContext.stop();
    }
    catch (...) {}

    // join threads
    for (auto& rThr : m_vThreads) {
        if (rThr.joinable()) rThr.join();
    }
    m_vThreads.clear();

    // recreate io_context for possible restart (optional; here we recreate)
    m_oIoContext.restart();
}
