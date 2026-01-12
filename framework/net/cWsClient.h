// cWsClient.h
#pragma once

#include <boost/asio.hpp>
#include <boost/beast.hpp>

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

class cWsClient {
public:
    using tOnMessage = std::function<void(const std::string&)>;
    using tOnState = std::function<void(bool connected, const std::string& reason)>;

    cWsClient();
    ~cWsClient();

    // thread-safe
    bool connect(const std::string& host, int port, const std::string& path = "/");
    void close(const std::string& reason = "close()");

    void setOnMessage(tOnMessage cb) { m_onMessage = std::move(cb); }
    void setOnState(tOnState cb) { m_onState = std::move(cb); }

    // thread-safe
    void sendText(const std::string& s);

    bool isConnected() const { return m_connected.load(std::memory_order_acquire); }

private:
    using tcp = boost::asio::ip::tcp;
    using ws_stream_t = boost::beast::websocket::stream<boost::beast::tcp_stream>;
    using work_guard_t = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;
    using executor_t = boost::asio::io_context::executor_type;
    using strand_t = boost::asio::strand<executor_t>;

    void ensureIoThread();
    void stopIoThreadNoThrow();

    // all below run on io thread / strand
    void doConnect(std::string host, int port, std::string path);
    void onResolve(boost::beast::error_code ec, tcp::resolver::results_type results);
    void onTcpConnect(boost::beast::error_code ec, tcp::resolver::results_type::endpoint_type);
    void onHandshake(boost::beast::error_code ec);

    void startRead();
    void onRead(boost::beast::error_code ec, std::size_t bytes);

    void queueWrite(std::string s);
    void startWrite();
    void onWrite(boost::beast::error_code ec, std::size_t bytes);

    void doClose(std::string reason);
    void onClose(boost::beast::error_code ec);

    void fail(const char* where, boost::beast::error_code ec);
    void setState(bool connected, const std::string& reason);

private:
    boost::asio::io_context m_ioc;
    tcp::resolver           m_resolver;

    strand_t                m_strand;
    std::unique_ptr<ws_stream_t> m_ws;
    boost::beast::flat_buffer    m_buffer;

    std::optional<work_guard_t>  m_work;
    std::thread                 m_thr;
    std::thread::id             m_ioThreadId{};

    tOnMessage m_onMessage;
    tOnState   m_onState;

    std::atomic<bool> m_connected{ false };
    std::atomic<bool> m_closing{ false };

    std::mutex m_qMtx;
    std::deque<std::string> m_writeQ;
    bool m_writeInFlight{ false };

    // params used only on io thread (for logging / reconnect info)
    std::string m_host;
    std::string m_path;
    int m_port{ 0 };
};
