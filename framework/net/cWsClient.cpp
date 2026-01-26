// cWsClient.cpp
#include "cWsClient.h"
#include "../../EduSpace/net_log.h"
#include <boost/beast/http.hpp>

namespace beast = boost::beast;
namespace websocket = beast::websocket;
using tcp = boost::asio::ip::tcp;

cWsClient::cWsClient()
    : m_resolver(m_ioc)
    , m_strand(boost::asio::make_strand(m_ioc))
{
}

cWsClient::~cWsClient() {
    close("~cWsClient");
}

void cWsClient::runThread()
{
    m_ioThreadId = std::this_thread::get_id();
    netlog::info("WSCLIENT", "io thread started");
    try { m_ioc.run(); }
    catch (const std::exception& e) { netlog::err("WSCLIENT", "io run exception: %s", e.what()); }
    catch (...) { netlog::err("WSCLIENT", "io run unknown exception"); }
    netlog::info("WSCLIENT", "io thread ended");
}

void cWsClient::ensureIoThread()
{
    // already running?
    if (m_work.has_value() && m_thr.joinable())
        return;

    m_ioc.restart();
    m_work.emplace(m_ioc.get_executor());

    // resolver must be bound to this ioc
    m_resolver = tcp::resolver(m_ioc);

    m_thr = std::thread([this] { runThread(); });
}

void cWsClient::stopIoThreadNoThrow()
{
    // called from non-io thread only
    try {
        m_work.reset();
        m_ioc.stop();
        if (m_thr.joinable())
            m_thr.join();
        m_ioc.restart();
    }
    catch (...) {
        // don't throw in destructor/close
    }
}

bool cWsClient::connect(const std::string& host, int port, const std::string& path)
{
    // Fully async connect. This function returns "request accepted", not "handshake ok".
    // If you need blocking connect, add a condition_variable + wait on onHandshake.
    close("connect() replace");

    m_closing.store(false, std::memory_order_release);
    m_connected.store(false, std::memory_order_release);

    try {
        ensureIoThread();

        // create ws on io thread
        boost::asio::post(m_strand, [this, h = std::string(host), p = port, pa = std::string(path)]() mutable {
            doConnect(std::move(h), p, std::move(pa));
            });

        return true;
    }
    catch (const std::exception& e) {
        netlog::err("WSCLIENT", "connect scheduling failed: %s", e.what());
        setState(false, e.what());
        return false;
    }
}

void cWsClient::doConnect(std::string host, int port, std::string path)
{
    if (m_closing.load(std::memory_order_acquire))
        return;

    m_host = std::move(host);
    m_port = port;
    m_path = std::move(path);

    // reset state
    m_buffer.clear();
    {
        std::lock_guard<std::mutex> lg(m_qMtx);
        m_writeQ.clear();
        m_writeInFlight = false;
    }

    // new ws stream
    m_ws = std::make_unique<ws_stream_t>(m_strand);

    // websocket options
    m_ws->set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
    m_ws->set_option(websocket::stream_base::decorator(
        [](websocket::request_type& req) {
            req.set(beast::http::field::user_agent, "cWsClient/beast");
        }));

    netlog::info("WSCLIENT", "resolve %s:%d ...", m_host.c_str(), m_port);
    m_resolver.async_resolve(
        m_host,
        std::to_string(m_port),
        beast::bind_front_handler(&cWsClient::onResolve, this));
}

void cWsClient::onResolve(beast::error_code ec, tcp::resolver::results_type results)
{
    if (m_closing.load(std::memory_order_acquire))
        return;

    if (ec) return fail("resolve", ec);
    if (!m_ws) return fail("resolve(ws null)", beast::error_code{});

    netlog::info("WSCLIENT", "tcp connect ...");
    beast::get_lowest_layer(*m_ws).expires_after(std::chrono::seconds(30));
    beast::get_lowest_layer(*m_ws).async_connect(
        results,
        beast::bind_front_handler(&cWsClient::onTcpConnect, this));
}

void cWsClient::onTcpConnect(beast::error_code ec, tcp::resolver::results_type::endpoint_type)
{
    if (m_closing.load(std::memory_order_acquire))
        return;

    if (ec) return fail("tcp_connect", ec);
    if (!m_ws) return fail("tcp_connect(ws null)", beast::error_code{});

    netlog::info("WSCLIENT", "ws handshake host=%s path=%s ...", m_host.c_str(), m_path.c_str());
    m_ws->async_handshake(
        m_host,
        m_path,
        beast::bind_front_handler(&cWsClient::onHandshake, this));
}

void cWsClient::onHandshake(beast::error_code ec)
{
    if (m_closing.load(std::memory_order_acquire))
        return;

    if (ec) return fail("handshake", ec);

    m_connected.store(true, std::memory_order_release);
    setState(true, "handshake ok");

    netlog::info("WSCLIENT", "CONNECTED ws://%s:%d%s", m_host.c_str(), m_port, m_path.c_str());

    startRead();
    startWrite(); // if something queued very early
}

void cWsClient::startRead()
{
    if (!m_ws) return;
    if (!m_connected.load(std::memory_order_acquire)) return;
    if (m_closing.load(std::memory_order_acquire)) return;

    m_ws->async_read(
        m_buffer,
        beast::bind_front_handler(&cWsClient::onRead, this));
}

void cWsClient::onRead(beast::error_code ec, std::size_t bytes)
{
    if (m_closing.load(std::memory_order_acquire))
        return;

    if (ec) {
        netlog::err("WSCLIENT", "read error: %s (%d)", ec.message().c_str(), ec.value());
        doClose(std::string("read error: ") + ec.message());
        return;
    }

    std::string msg = beast::buffers_to_string(m_buffer.cdata());
    m_buffer.consume(bytes);

    netlog::info("WSCLIENT", "<- %s", msg.c_str());
    if (m_onMessage) m_onMessage(msg);

    startRead();
}

void cWsClient::sendText(const std::string& s)
{
    if (m_closing.load(std::memory_order_acquire)) {
        netlog::err("WSCLIENT", "sendText ignored: closing");
        return;
    }

    // allow queueing even before connected; it will flush after handshake
    queueWrite(std::string(s));
}

void cWsClient::queueWrite(std::string s)
{
    {
        std::lock_guard<std::mutex> lg(m_qMtx);
        m_writeQ.push_back(std::move(s));
    }

    // always bounce to strand, safe from any thread
    boost::asio::post(m_strand, [this] { startWrite(); });
}

void cWsClient::startWrite()
{
    if (!m_ws) return;
    if (!m_connected.load(std::memory_order_acquire)) return;
    if (m_closing.load(std::memory_order_acquire)) return;

    std::string msg;
    {
        std::lock_guard<std::mutex> lg(m_qMtx);
        if (m_writeInFlight) return;
        if (m_writeQ.empty()) return;

        m_writeInFlight = true;
        msg = std::move(m_writeQ.front());
        m_writeQ.pop_front();
    }

    netlog::info("WSCLIENT", "-> %s", msg.c_str());
    m_ws->text(true);
    m_ws->async_write(
        boost::asio::buffer(msg),
        beast::bind_front_handler(&cWsClient::onWrite, this));
}

void cWsClient::onWrite(beast::error_code ec, std::size_t)
{
    {
        std::lock_guard<std::mutex> lg(m_qMtx);
        m_writeInFlight = false;
    }

    if (m_closing.load(std::memory_order_acquire))
        return;

    if (ec) {
        netlog::err("WSCLIENT", "write error: %s (%d)", ec.message().c_str(), ec.value());
        doClose(std::string("write error: ") + ec.message());
        return;
    }

    startWrite();
}

void cWsClient::close(const std::string& reason)
{
    bool expected = false;
    if (!m_closing.compare_exchange_strong(expected, true))
        return;

    bool wasConnected = m_connected.exchange(false);
    if (wasConnected) {
        netlog::info("WSCLIENT", "closing: %s", reason.c_str());
        setState(false, reason);
    }

    // if thread not started - still must clear queue safely
    if (!m_work.has_value()) {
        m_ws.reset();
        m_buffer.clear();
        {
            std::lock_guard<std::mutex> lg(m_qMtx);
            m_writeQ.clear();
            m_writeInFlight = false;
        }
        return;
    }

    // close on io thread/strand
    boost::asio::post(m_strand, [this, r = std::string(reason)]() mutable {
        doClose(std::move(r));
        });

    // don't join from io thread
    if (std::this_thread::get_id() == m_ioThreadId)
        return;

    // stop thread after scheduling close
    stopIoThreadNoThrow();

    // final cleanup (single-thread now)
    m_ws.reset();
    m_buffer.clear();
    {
        std::lock_guard<std::mutex> lg(m_qMtx);
        m_writeQ.clear();
        m_writeInFlight = false;
    }

    netlog::info("WSCLIENT", "closed (%s)", reason.c_str());
}

void cWsClient::doClose(std::string reason)
{
    // io thread only
    beast::error_code ec;

    if (m_ws && m_ws->is_open()) {
        m_ws->async_close(
            websocket::close_reason(reason),
            beast::bind_front_handler(&cWsClient::onClose, this));
        return;
    }

    // already closed
    m_ws.reset();
}

void cWsClient::onClose(beast::error_code ec)
{
    if (ec) {
        netlog::err("WSCLIENT", "ws close error: %s (%d)", ec.message().c_str(), ec.value());
    }

    m_connected.store(false, std::memory_order_release);
    m_ws.reset();
}

void cWsClient::fail(const char* where, beast::error_code ec)
{
    if (ec) {
        netlog::err("WSCLIENT", "%s failed: %s (%d)", where, ec.message().c_str(), ec.value());
        setState(false, std::string(where) + ": " + ec.message());
    }
    else {
        // generic
        netlog::err("WSCLIENT", "%s failed", where);
        setState(false, std::string(where) + ": failed");
    }

    m_connected.store(false, std::memory_order_release);
    m_closing.store(true, std::memory_order_release);

    // best-effort cleanup on io thread
    doClose(std::string(where));
}

void cWsClient::setState(bool connected, const std::string& reason)
{
    // NOTE: this callback may be called from io thread.
    // If you need UI-thread-only, wrap this and forward to your UI dispatcher.
    if (m_onState) m_onState(connected, reason);
}
