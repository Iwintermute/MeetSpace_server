#include "Bridge/Mediasoup/runtime/MediasoupBackendReadinessProbe.h"

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/websocket/stream.hpp>
#include <boost/system/system_error.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <string>
#include <string_view>

namespace eds::server_new::mediasoup::runtime {
namespace {

using json = nlohmann::json;
using tcp = boost::asio::ip::tcp;
using websocket_stream = boost::beast::websocket::stream<boost::beast::tcp_stream>;

struct BackendUrlParts {
    std::string host;
    std::string port;
    std::string path = "/";
};

bool parseBackendUrl(std::string_view url, BackendUrlParts& parts) {
    constexpr std::string_view wsPrefix = "ws://";
    if (url.rfind(wsPrefix, 0) != 0) {
        return false;
    }

    const auto remaining = url.substr(wsPrefix.size());
    const auto slashPos = remaining.find('/');
    const auto hostPort = slashPos == std::string_view::npos
        ? remaining
        : remaining.substr(0, slashPos);
    parts.path = slashPos == std::string_view::npos
        ? "/"
        : std::string(remaining.substr(slashPos));

    const auto colonPos = hostPort.rfind(':');
    if (colonPos == std::string_view::npos) {
        parts.host = std::string(hostPort);
        parts.port = "80";
    } else {
        parts.host = std::string(hostPort.substr(0, colonPos));
        parts.port = std::string(hostPort.substr(colonPos + 1));
    }
    if (parts.host.empty() || parts.port.empty()) {
        return false;
    }
    if (parts.path.empty()) {
        parts.path = "/";
    }
    return true;
}

std::string toLowerCopy(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char symbol) {
        return static_cast<char>(std::tolower(symbol));
    });
    return result;
}

bool isMediasoupEngineName(std::string_view engine) {
    if (engine.empty()) {
        return false;
    }
    return toLowerCopy(engine).find("mediasoup") != std::string::npos;
}

std::string makeRequestId() {
    const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return "dev-probe-" + std::to_string(nowMs);
}

} // namespace

MediasoupBackendProbeResult MediasoupBackendReadinessProbe::probe(
    std::string_view backendUrl,
    std::chrono::milliseconds timeout) {
    MediasoupBackendProbeResult result;
    BackendUrlParts urlParts;
    if (!parseBackendUrl(backendUrl, urlParts)) {
        result.message = "Invalid mediasoup backend URL. Expected ws://host:port/path.";
        return result;
    }
    if (timeout.count() <= 0) {
        timeout = std::chrono::milliseconds(1000);
    }

    try {
        boost::asio::io_context ioContext;
        tcp::resolver resolver(ioContext);
        websocket_stream socket(ioContext);

        const auto endpoints = resolver.resolve(urlParts.host, urlParts.port);
        boost::beast::get_lowest_layer(socket).expires_after(timeout);
        boost::beast::get_lowest_layer(socket).connect(endpoints);

        boost::beast::get_lowest_layer(socket).expires_after(timeout);
        socket.handshake(urlParts.host + ":" + urlParts.port, urlParts.path);

        const json request{
            { "id", makeRequestId() },
            { "operation", "system.getCapabilities" },
            { "payload", {
                { "requireEngine", "mediasoup" },
                { "requireRouterRtpCapabilities", true }
            } }
        };
        const auto serializedRequest = request.dump();

        boost::beast::get_lowest_layer(socket).expires_after(timeout);
        socket.write(boost::asio::buffer(serializedRequest));

        boost::beast::flat_buffer responseBuffer;
        boost::beast::get_lowest_layer(socket).expires_after(timeout);
        socket.read(responseBuffer);

        boost::system::error_code closeError;
        socket.close(boost::beast::websocket::close_code::normal, closeError);

        const auto responseText = boost::beast::buffers_to_string(responseBuffer.data());
        const auto response = json::parse(responseText, nullptr, false);
        if (!response.is_object()) {
            result.message = "Mediasoup backend capability response is not a JSON object.";
            return result;
        }
        if (!response.contains("ok") || !response["ok"].is_boolean()) {
            result.message = "Mediasoup backend capability response must contain boolean field 'ok'.";
            return result;
        }
        if (!response["ok"].get<bool>()) {
            result.message = response.value("message", std::string("Mediasoup capability check failed."));
            return result;
        }

        const auto backend = response.contains("backend") && response["backend"].is_object()
            ? response["backend"]
            : response;
        result.engine = backend.value("engine", std::string{});
        result.version = backend.value("version", response.value("version", std::string{}));
        if (!isMediasoupEngineName(result.engine)) {
            result.message = "Backend engine mismatch. Expected mediasoup-compatible engine.";
            return result;
        }
        if (!backend.contains("routerRtpCapabilities") || !backend["routerRtpCapabilities"].is_object()) {
            result.message = "Capability response does not contain routerRtpCapabilities.";
            return result;
        }

        result.ok = true;
        result.message = "Mediasoup backend is ready.";
        return result;
    } catch (const boost::system::system_error& error) {
        result.message = std::string("Mediasoup backend probe failed: ") + error.what();
        return result;
    } catch (const std::exception& error) {
        result.message = std::string("Mediasoup backend probe failed: ") + error.what();
        return result;
    }
}

} // namespace eds::server_new::mediasoup::runtime
