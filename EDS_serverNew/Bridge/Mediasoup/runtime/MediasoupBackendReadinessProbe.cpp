#include "Bridge/Mediasoup/runtime/MediasoupBackendReadinessProbe.h"

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket/stream.hpp>
#include <boost/system/system_error.hpp>
#include <nlohmann/json.hpp>
#include <openssl/ssl.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <initializer_list>
#include <string>
#include <string_view>

namespace eds::server_new::mediasoup::runtime {
namespace {

using json = nlohmann::json;
using tcp = boost::asio::ip::tcp;
using websocket_stream_plain = boost::beast::websocket::stream<boost::beast::tcp_stream>;
using websocket_stream_tls = boost::beast::websocket::stream<boost::beast::ssl_stream<boost::beast::tcp_stream>>;

struct BackendUrlParts {
    std::string host;
    std::string port;
    std::string path = "/";
    bool secure = false;
};

std::string readEnvVar(const char* name) {
    const auto value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return {};
    }
    return std::string(value);
}

std::string readFirstEnvVar(std::initializer_list<const char*> names) {
    for (const auto* name : names) {
        if (name == nullptr || name[0] == '\0') {
            continue;
        }
        auto value = readEnvVar(name);
        if (!value.empty()) {
            return value;
        }
    }
    return {};
}

bool parseBooleanOption(std::string value, bool& result) {
    if (value.empty()) {
        return false;
    }

    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char symbol) {
        return static_cast<char>(std::tolower(symbol));
        });
    if (value == "1" || value == "true" || value == "yes" || value == "on") {
        result = true;
        return true;
    }
    if (value == "0" || value == "false" || value == "no" || value == "off") {
        result = false;
        return true;
    }
    return false;
}

bool readFirstBooleanEnvVar(std::initializer_list<const char*> names, bool fallbackValue) {
    for (const auto* name : names) {
        if (name == nullptr || name[0] == '\0') {
            continue;
        }

        auto value = readEnvVar(name);
        if (value.empty()) {
            continue;
        }

        bool parsed = fallbackValue;
        if (parseBooleanOption(value, parsed)) {
            return parsed;
        }
    }

    return fallbackValue;
}

bool parseBackendUrl(std::string_view url, BackendUrlParts& parts) {
    constexpr std::string_view wsPrefix = "ws://";
    constexpr std::string_view wssPrefix = "wss://";

    std::string_view remaining;
    if (url.rfind(wsPrefix, 0) == 0) {
        remaining = url.substr(wsPrefix.size());
        parts.secure = false;
    }
    else if (url.rfind(wssPrefix, 0) == 0) {
        remaining = url.substr(wssPrefix.size());
        parts.secure = true;
    }
    else {
        return false;
    }

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
        parts.port = parts.secure ? "443" : "80";
    }
    else {
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

std::string stripIpv6Brackets(std::string_view host) {
    if (host.size() >= 2 && host.front() == '[' && host.back() == ']') {
        return std::string(host.substr(1, host.size() - 2));
    }
    return std::string(host);
}

bool isPrivateOrLoopbackAddress(const boost::asio::ip::address& address) {
    if (address.is_v4()) {
        const auto bytes = address.to_v4().to_bytes();
        if (bytes[0] == 127) {
            return true;
        }
        if (bytes[0] == 10) {
            return true;
        }
        if (bytes[0] == 172 && bytes[1] >= 16 && bytes[1] <= 31) {
            return true;
        }
        if (bytes[0] == 192 && bytes[1] == 168) {
            return true;
        }
        if (bytes[0] == 169 && bytes[1] == 254) {
            return true;
        }
        return false;
    }

    const auto addressV6 = address.to_v6();
    if (addressV6.is_loopback()) {
        return true;
    }

    const auto bytes = addressV6.to_bytes();
    if ((bytes[0] & 0xFE) == 0xFC) {
        return true;
    }
    if (bytes[0] == 0xFE && (bytes[1] & 0xC0) == 0x80) {
        return true;
    }
    return false;
}

bool isPrivateOrLoopbackHost(std::string_view host) {
    if (host.empty()) {
        return false;
    }

    const auto normalizedHost = stripIpv6Brackets(host);
    if (normalizedHost.empty()) {
        return false;
    }
    if (toLowerCopy(normalizedHost) == "localhost") {
        return true;
    }

    boost::system::error_code addressCode;
    const auto parsedAddress = boost::asio::ip::make_address(normalizedHost, addressCode);
    if (addressCode) {
        return false;
    }
    return isPrivateOrLoopbackAddress(parsedAddress);
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

json buildCapabilitiesRequest() {
    return json{
        { "id", makeRequestId() },
        { "operation", "system.getCapabilities" },
        { "payload", {
            { "requireEngine", "mediasoup" },
            { "requireRouterRtpCapabilities", true }
        } }
    };
}

template <typename TSocket>
std::string performCapabilitiesExchange(TSocket& socket, std::chrono::milliseconds timeout) {
    const auto serializedRequest = buildCapabilitiesRequest().dump();
    boost::beast::get_lowest_layer(socket).expires_after(timeout);
    socket.write(boost::asio::buffer(serializedRequest));

    boost::beast::flat_buffer responseBuffer;
    boost::beast::get_lowest_layer(socket).expires_after(timeout);
    socket.read(responseBuffer);

    return boost::beast::buffers_to_string(responseBuffer.data());
}

bool validateCapabilitiesResponse(
    const json& response,
    MediasoupBackendProbeResult& result) {
    if (!response.is_object()) {
        result.message = "Mediasoup backend capability response is not a JSON object.";
        return false;
    }
    if (!response.contains("ok") || !response["ok"].is_boolean()) {
        result.message = "Mediasoup backend capability response must contain boolean field 'ok'.";
        return false;
    }
    if (!response["ok"].get<bool>()) {
        result.message = response.value("message", std::string("Mediasoup capability check failed."));
        return false;
    }

    const auto backend = response.contains("backend") && response["backend"].is_object()
        ? response["backend"]
        : response;
    result.engine = backend.value("engine", std::string{});
    result.version = backend.value("version", response.value("version", std::string{}));
    if (!isMediasoupEngineName(result.engine)) {
        result.message = "Backend engine mismatch. Expected mediasoup-compatible engine.";
        return false;
    }
    if (!backend.contains("routerRtpCapabilities") || !backend["routerRtpCapabilities"].is_object()) {
        result.message = "Capability response does not contain routerRtpCapabilities.";
        return false;
    }

    return true;
}

} // namespace

MediasoupBackendProbeResult MediasoupBackendReadinessProbe::probe(
    std::string_view backendUrl,
    std::chrono::milliseconds timeout) {
    MediasoupBackendProbeResult result;
    BackendUrlParts urlParts;
    if (!parseBackendUrl(backendUrl, urlParts)) {
        result.message = "Invalid mediasoup backend URL. Expected ws://host:port/path or wss://host:port/path.";
        return result;
    }
    if (!urlParts.secure && !isPrivateOrLoopbackHost(urlParts.host)) {
        result.message =
            "Remote mediasoup backend endpoint must use wss://. ws:// is allowed only for localhost/private hosts.";
        return result;
    }
    if (timeout.count() <= 0) {
        timeout = std::chrono::milliseconds(1000);
    }

    try {
        boost::asio::io_context ioContext;
        tcp::resolver resolver(ioContext);
        const auto endpoints = resolver.resolve(urlParts.host, urlParts.port);

        std::string responseText;
        if (urlParts.secure) {
            const bool insecureSkipVerify = readFirstBooleanEnvVar(
                {
                    "MEETSPACE_MEDIASOUP_BACKEND_TLS_INSECURE_SKIP_VERIFY",
                    "EDUSPACE_MEDIASOUP_BACKEND_TLS_INSECURE_SKIP_VERIFY",
                    "MEDIASOUP_BACKEND_TLS_INSECURE_SKIP_VERIFY"
                },
                false);
            const auto tlsCaFile = readFirstEnvVar(
                {
                    "MEETSPACE_MEDIASOUP_BACKEND_TLS_CA_FILE",
                    "EDUSPACE_MEDIASOUP_BACKEND_TLS_CA_FILE",
                    "MEDIASOUP_BACKEND_TLS_CA_FILE"
                });
            auto tlsServerName = readFirstEnvVar(
                {
                    "MEETSPACE_MEDIASOUP_BACKEND_TLS_SERVER_NAME",
                    "EDUSPACE_MEDIASOUP_BACKEND_TLS_SERVER_NAME",
                    "MEDIASOUP_BACKEND_TLS_SERVER_NAME"
                });
            if (tlsServerName.empty()) {
                tlsServerName = urlParts.host;
            }

            boost::asio::ssl::context tlsContext(boost::asio::ssl::context::tls_client);
            boost::system::error_code tlsError;

            if (!insecureSkipVerify) {
                if (!tlsCaFile.empty()) {
                    tlsContext.load_verify_file(tlsCaFile, tlsError);
                    if (tlsError) {
                        result.message = std::string("Mediasoup backend probe TLS CA load failed: ") + tlsError.message();
                        return result;
                    }
                }
                else {
                    tlsContext.set_default_verify_paths(tlsError);
                    if (tlsError) {
                        result.message = std::string("Mediasoup backend probe TLS trust store setup failed: ")
                            + tlsError.message();
                        return result;
                    }
                }

                tlsContext.set_verify_mode(boost::asio::ssl::verify_peer, tlsError);
                if (tlsError) {
                    result.message = std::string("Mediasoup backend probe TLS verify mode setup failed: ")
                        + tlsError.message();
                    return result;
                }
            }
            else {
                tlsContext.set_verify_mode(boost::asio::ssl::verify_none, tlsError);
                if (tlsError) {
                    result.message = std::string("Mediasoup backend probe TLS verify mode setup failed: ")
                        + tlsError.message();
                    return result;
                }
            }

            websocket_stream_tls socket(ioContext, tlsContext);
            boost::beast::get_lowest_layer(socket).expires_after(timeout);
            boost::beast::get_lowest_layer(socket).connect(endpoints);

            if (!insecureSkipVerify && !tlsServerName.empty()) {
                if (!SSL_set_tlsext_host_name(socket.next_layer().native_handle(), tlsServerName.c_str())) {
                    result.message = "Mediasoup backend probe failed to configure TLS SNI host name.";
                    return result;
                }
            }

            boost::beast::get_lowest_layer(socket).expires_after(timeout);
            socket.next_layer().handshake(boost::asio::ssl::stream_base::client);

            boost::beast::get_lowest_layer(socket).expires_after(timeout);
            socket.handshake(urlParts.host + ":" + urlParts.port, urlParts.path);

            responseText = performCapabilitiesExchange(socket, timeout);

            boost::system::error_code closeError;
            socket.close(boost::beast::websocket::close_code::normal, closeError);
            boost::system::error_code shutdownError;
            socket.next_layer().shutdown(shutdownError);
        }
        else {
            websocket_stream_plain socket(ioContext);
            boost::beast::get_lowest_layer(socket).expires_after(timeout);
            boost::beast::get_lowest_layer(socket).connect(endpoints);

            boost::beast::get_lowest_layer(socket).expires_after(timeout);
            socket.handshake(urlParts.host + ":" + urlParts.port, urlParts.path);

            responseText = performCapabilitiesExchange(socket, timeout);

            boost::system::error_code closeError;
            socket.close(boost::beast::websocket::close_code::normal, closeError);
        }

        const auto response = json::parse(responseText, nullptr, false);
        if (!validateCapabilitiesResponse(response, result)) {
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
