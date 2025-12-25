#include "cNetIoContext.h"
#include <algorithm>
#include <iostream>

using namespace Sys::Network;

cNetIoContext::cNetIoContext()
    : m_oIoContext(),
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
    return true;
}

bool cNetIoContext::fnStart()
{
    if (m_bRunning) return true;
    m_bRunning = true;

    return true;
}

void cNetIoContext::fnStop()
{
    m_bRunning = false;
}
