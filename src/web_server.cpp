#include "echoir/web_server.hpp"

#include "echoir_web_assets.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
#endif

namespace echoir {
namespace {

struct HttpRequest {
    std::string method;
    std::string path;
    std::string body;
};

struct HttpResponse {
    int status = 200;
    std::string statusText = "OK";
    std::string contentType = "application/json; charset=utf-8";
    std::string body;
};

struct Listener {
    SocketHandle socket = kInvalidSocket;
    std::string host;
};

#ifdef _WIN32
class WinsockGuard {
public:
    WinsockGuard() {
        WSADATA data {};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            throw std::runtime_error("WSAStartup failed");
        }
    }

    ~WinsockGuard() {
        WSACleanup();
    }
};

void closeSocket(SocketHandle socket) {
    closesocket(socket);
}
#else
class WinsockGuard {
};

void closeSocket(SocketHandle socket) {
    close(socket);
}
#endif

SocketHandle createListener(const std::string& host, int port) {
    addrinfo hints {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* results = nullptr;
    const auto service = std::to_string(port);
    const int lookup = getaddrinfo(host.c_str(), service.c_str(), &hints, &results);
    if (lookup != 0 || results == nullptr) {
        throw std::runtime_error("failed to resolve web host: " + host);
    }

    SocketHandle listener = kInvalidSocket;
    for (auto* item = results; item != nullptr; item = item->ai_next) {
        listener = socket(item->ai_family, item->ai_socktype, item->ai_protocol);
        if (listener == kInvalidSocket) {
            continue;
        }
        int reuse = 1;
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
        if (bind(listener, item->ai_addr, static_cast<int>(item->ai_addrlen)) == 0 &&
            listen(listener, 16) == 0) {
            break;
        }
        closeSocket(listener);
        listener = kInvalidSocket;
    }

    freeaddrinfo(results);
    if (listener == kInvalidSocket) {
        throw std::runtime_error("failed to bind web host: " + host);
    }
    return listener;
}

std::string canonicalHostAddress(const std::string& host) {
    addrinfo hints {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* results = nullptr;
    const int lookup = getaddrinfo(host.c_str(), nullptr, &hints, &results);
    if (lookup != 0 || results == nullptr) {
        throw std::runtime_error("failed to resolve web host: " + host);
    }

    std::string out;
    for (auto* item = results; item != nullptr; item = item->ai_next) {
        if (item->ai_family != AF_INET) {
            continue;
        }
        auto* address = &reinterpret_cast<sockaddr_in*>(item->ai_addr)->sin_addr;
#ifdef _WIN32
        const auto* buffer = inet_ntoa(*address);
        if (buffer != nullptr) {
            out = buffer;
            break;
        }
#else
        char buffer[INET_ADDRSTRLEN] {};
        if (inet_ntop(AF_INET, address, buffer, sizeof(buffer)) != nullptr) {
            out = buffer;
            break;
        }
#endif
    }
    freeaddrinfo(results);
    if (out.empty()) {
        throw std::runtime_error("failed to resolve web host: " + host);
    }
    return out;
}

std::string jsonEscape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (const unsigned char c : value) {
        switch (c) {
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (c < 0x20) {
                    out += ' ';
                } else {
                    out += static_cast<char>(c);
                }
                break;
        }
    }
    return out;
}

std::string jsonString(const std::string& value) {
    return "\"" + jsonEscape(value) + "\"";
}

std::string jsonArray(const std::vector<std::string>& values) {
    std::string out = "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            out += ",";
        }
        out += jsonString(values[i]);
    }
    out += "]";
    return out;
}

std::string trim(std::string text) {
    const auto begin = std::find_if_not(text.begin(), text.end(), [](unsigned char c) {
        return std::isspace(c) != 0;
    });
    const auto end = std::find_if_not(text.rbegin(), text.rend(), [](unsigned char c) {
        return std::isspace(c) != 0;
    }).base();
    if (begin >= end) {
        return {};
    }
    return std::string(begin, end);
}

std::string unescapeJsonString(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    bool escape = false;
    for (const char c : value) {
        if (!escape) {
            if (c == '\\') {
                escape = true;
            } else {
                out += c;
            }
            continue;
        }
        switch (c) {
            case 'n':
                out += '\n';
                break;
            case 'r':
                out += '\r';
                break;
            case 't':
                out += '\t';
                break;
            default:
                out += c;
                break;
        }
        escape = false;
    }
    return out;
}

std::map<std::string, std::string> parseFlatJsonObject(const std::string& body) {
    std::map<std::string, std::string> values;
    const std::regex item("\"([^\"]+)\"\\s*:\\s*(\"((?:\\\\.|[^\"])*)\"|true|false|null|-?[0-9]+)");
    for (std::sregex_iterator it(body.begin(), body.end(), item), end; it != end; ++it) {
        const std::string key = (*it)[1].str();
        const std::string stringValue = (*it)[3].str();
        if ((*it)[2].str().empty() || (*it)[2].str() == "null") {
            continue;
        }
        if ((*it)[2].str().front() == '"') {
            values[key] = unescapeJsonString(stringValue);
        } else {
            values[key] = (*it)[2].str();
        }
    }
    return values;
}

std::string optionValue(const std::map<std::string, std::string>& body,
                        const std::string& key,
                        const std::string& fallback = {}) {
    const auto it = body.find(key);
    return it == body.end() ? fallback : it->second;
}

bool truthy(const std::map<std::string, std::string>& body, const std::string& key) {
    const auto value = optionValue(body, key);
    return value == "true" || value == "1" || value == "on";
}

void copyIfPresent(std::map<std::string, std::string>& options,
                   const std::map<std::string, std::string>& body,
                   const std::string& from,
                   const std::string& to) {
    const auto it = body.find(from);
    if (it != body.end() && !it->second.empty()) {
        options[to] = it->second;
    }
}

std::map<std::string, std::string> commonOptions(const std::map<std::string, std::string>& body,
                                                 const WebServerConfig& config) {
    std::map<std::string, std::string> options;
    if (!optionValue(body, "port").empty()) {
        options["port"] = optionValue(body, "port");
    } else if (!config.serialPort.empty()) {
        options["port"] = config.serialPort;
    }
    copyIfPresent(options, body, "baud", "baud");
    copyIfPresent(options, body, "address", "address");
    copyIfPresent(options, body, "profile", "profile");
    return options;
}

std::string commandLine(const std::string& command, const std::map<std::string, std::string>& options) {
    std::string line = "echoir " + command;
    for (const auto& [key, value] : options) {
        line += " --" + key;
        if (value != "true") {
            line += " " + value;
        }
    }
    return line;
}

std::string resultJson(const std::string& command,
                       const std::map<std::string, std::string>& options,
                       const WebCommandResult& result) {
    return std::string("{\"ok\":true,\"result\":{") +
           "\"command\":" + jsonString(commandLine(command, options)) + "," +
           "\"stdout\":" + jsonString(trim(result.stdoutText)) + "," +
           "\"stderr\":\"\"," +
           "\"exitCode\":" + std::to_string(result.exitCode) + "," +
           "\"timedOut\":false}}";
}

std::vector<std::string> splitCsv(const std::string& text) {
    std::vector<std::string> out;
    std::stringstream stream(text);
    std::string item;
    while (std::getline(stream, item, ',')) {
        item = trim(item);
        if (!item.empty()) {
            out.push_back(item);
        }
    }
    return out;
}

std::string safeFileName(const std::string& file) {
    const auto base = std::filesystem::path(file).filename().string();
    if (!std::regex_match(base, std::regex("[A-Za-z0-9_.-]+\\.json"))) {
        throw std::invalid_argument("code file is invalid");
    }
    return base;
}

std::string safeCodeName(const std::string& name) {
    std::string out;
    for (const unsigned char c : name) {
        if (std::isalnum(c) || c == '_' || c == '-' || c == '.') {
            out += static_cast<char>(c);
        } else if (!out.empty() && out.back() != '_') {
            out += '_';
        }
    }
    out = trim(out);
    if (out.empty()) {
        out = "learned";
    }
    if (out.size() > 64) {
        out.resize(64);
    }
    return out;
}

std::string safeCodePath(const WebServerConfig& config, const std::string& file) {
    return (std::filesystem::path(config.codeDirectory) / safeFileName(file)).string();
}

std::string codePathForName(const WebServerConfig& config, const std::string& name) {
    return (std::filesystem::path(config.codeDirectory) / (safeCodeName(name) + ".json")).string();
}

std::vector<std::string> listCodes(const WebServerConfig& config) {
    std::vector<std::string> codes;
    std::filesystem::create_directories(config.codeDirectory);
    for (const auto& entry : std::filesystem::directory_iterator(config.codeDirectory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            codes.push_back(entry.path().filename().string());
        }
    }
    std::sort(codes.begin(), codes.end());
    return codes;
}

std::string mappedApiResponse(const std::string& path,
                              const std::map<std::string, std::string>& body,
                              const WebServerConfig& config,
                              const WebCommandRunner& runner) {
    if (path == "/api/health") {
        std::map<std::string, std::string> options {{"list-protocols", "true"}};
        const auto result = runner("ac-official", options);
        return std::string("{\"ok\":true,\"cliPath\":\"embedded\",\"protocols\":") +
               jsonArray(splitCsv(result.stdoutText)) + ",\"serialPort\":" +
               jsonString(config.serialPort) + ",\"detectedPorts\":" +
               jsonArray(config.detectedSerialPorts) + "}";
    }

    if (path == "/api/codes") {
        return std::string("{\"ok\":true,\"codes\":") + jsonArray(listCodes(config)) + "}";
    }

    std::string command;
    auto options = commonOptions(body, config);

    if (path == "/api/device/info") {
        command = "info";
    } else if (path == "/api/device/reset") {
        command = "reset";
    } else if (path == "/api/ac/dry-run" || path == "/api/ac/send") {
        command = "ac";
        if (!optionValue(body, "brandCode").empty()) {
            options["brand-code"] = optionValue(body, "brandCode");
        } else {
            options["brand"] = optionValue(body, "brand", "gree1");
        }
        options["power"] = optionValue(body, "power", "on");
        options["mode"] = optionValue(body, "mode", "cool");
        options["temp"] = optionValue(body, "temp", "24");
        options["fan"] = optionValue(body, "fan", "auto");
        if (truthy(body, "waitAck")) {
            options["wait-ack"] = "true";
        }
        if (path == "/api/ac/dry-run") {
            options["dry-run"] = "true";
        }
    } else if (path == "/api/ac-official/dry-run" || path == "/api/ac-official/send") {
        command = "ac-official";
        options["protocol"] = optionValue(body, "protocol", "gree3");
        copyIfPresent(options, body, "power", "power");
        copyIfPresent(options, body, "mode", "mode");
        copyIfPresent(options, body, "temp", "temp");
        copyIfPresent(options, body, "fan", "fan");
        copyIfPresent(options, body, "swingIndex", "swing-index");
        copyIfPresent(options, body, "timerIndex", "timer-index");
        copyIfPresent(options, body, "fields", "fields");
        if (truthy(body, "waitAck")) {
            options["wait-ack"] = "true";
        }
        if (path == "/api/ac-official/dry-run") {
            options["dry-run"] = "true";
        }
    } else if (path == "/api/internal/learn") {
        command = "learn-internal";
        options["slot"] = optionValue(body, "slot", "0");
        options["timeout-ms"] = optionValue(body, "timeoutMs", "15000");
    } else if (path == "/api/internal/send") {
        command = "send-internal";
        options["slot"] = optionValue(body, "slot", "0");
    } else if (path == "/api/external/learn") {
        command = "learn-external";
        const auto name = optionValue(body, "name", "learned");
        const auto out = codePathForName(config, name);
        options["name"] = name;
        options["out"] = out;
        options["timeout-ms"] = optionValue(body, "timeoutMs", "15000");
    } else if (path == "/api/external/send") {
        command = "send-external";
        options["in"] = safeCodePath(config, optionValue(body, "file"));
    } else if (path == "/api/external/dump") {
        command = "dump";
        options["in"] = safeCodePath(config, optionValue(body, "file"));
    } else {
        throw std::invalid_argument("unknown API endpoint");
    }

    const auto result = runner(command, options);
    return resultJson(command, options, result);
}

std::string percentDecode(const std::string& value) {
    std::string out;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            const auto hex = value.substr(i + 1, 2);
            char* end = nullptr;
            const auto parsed = std::strtol(hex.c_str(), &end, 16);
            if (end != nullptr && *end == '\0') {
                out += static_cast<char>(parsed);
                i += 2;
                continue;
            }
        }
        out += value[i];
    }
    return out;
}

HttpRequest readRequest(SocketHandle socket) {
    std::string data;
    char buffer[4096];
    while (data.find("\r\n\r\n") == std::string::npos) {
        const auto received = recv(socket, buffer, sizeof(buffer), 0);
        if (received <= 0) {
            throw std::runtime_error("failed to read HTTP request");
        }
        data.append(buffer, buffer + received);
        if (data.size() > 4 * 1024 * 1024) {
            throw std::runtime_error("HTTP request is too large");
        }
    }

    const auto headerEnd = data.find("\r\n\r\n");
    const auto header = data.substr(0, headerEnd);
    std::istringstream stream(header);
    HttpRequest request;
    std::string target;
    stream >> request.method >> target;
    const auto query = target.find('?');
    request.path = percentDecode(query == std::string::npos ? target : target.substr(0, query));

    std::string line;
    std::size_t contentLength = 0;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const auto colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        auto key = line.substr(0, colon);
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (key == "content-length") {
            contentLength = static_cast<std::size_t>(std::stoul(trim(line.substr(colon + 1))));
        }
    }

    request.body = data.substr(headerEnd + 4);
    while (request.body.size() < contentLength) {
        const auto received = recv(socket, buffer, sizeof(buffer), 0);
        if (received <= 0) {
            throw std::runtime_error("failed to read HTTP body");
        }
        request.body.append(buffer, buffer + received);
    }
    if (request.body.size() > contentLength) {
        request.body.resize(contentLength);
    }
    return request;
}

HttpResponse handleRequest(const HttpRequest& request,
                           const WebServerConfig& config,
                           const WebCommandRunner& runner) {
    if (request.path.rfind("/api/", 0) == 0) {
        const auto body = request.method == "POST" ? parseFlatJsonObject(request.body)
                                                   : std::map<std::string, std::string> {};
        return {200, "OK", "application/json; charset=utf-8", mappedApiResponse(request.path, body, config, runner)};
    }

    if (request.method != "GET") {
        return {405, "Method Not Allowed", "text/plain; charset=utf-8", "method not allowed"};
    }

    std::string assetPath = request.path == "/" ? "/index.html" : request.path;
    const auto* asset = findEmbeddedWebAsset(assetPath);
    if (asset == nullptr && assetPath.find('.') == std::string::npos) {
        asset = findEmbeddedWebAsset("/index.html");
    }
    if (asset == nullptr) {
        return {404, "Not Found", "text/plain; charset=utf-8", "not found"};
    }
    return {200,
            "OK",
            asset->mimeType,
            std::string(reinterpret_cast<const char*>(asset->data), asset->size)};
}

void sendAll(SocketHandle socket, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const auto chunk = data.size() - sent;
        const auto written = send(socket, data.data() + sent, static_cast<int>(chunk), 0);
        if (written <= 0) {
            throw std::runtime_error("failed to write HTTP response");
        }
        sent += static_cast<std::size_t>(written);
    }
}

void sendResponse(SocketHandle socket, const HttpResponse& response) {
    std::ostringstream headers;
    headers << "HTTP/1.1 " << response.status << ' ' << response.statusText << "\r\n"
            << "Content-Type: " << response.contentType << "\r\n"
            << "Content-Length: " << response.body.size() << "\r\n"
            << "Cache-Control: no-cache\r\n"
            << "Connection: close\r\n\r\n";
    sendAll(socket, headers.str());
    sendAll(socket, response.body);
}

HttpResponse errorResponse(const std::exception& error) {
    return {400,
            "Bad Request",
            "application/json; charset=utf-8",
            std::string("{\"ok\":false,\"error\":") + jsonString(error.what()) + "}"};
}

}  // namespace

void runEmbeddedWebServer(const WebServerConfig& config, const WebCommandRunner& runner) {
    WinsockGuard winsock;
    std::filesystem::create_directories(config.codeDirectory);

    if (config.hosts.empty()) {
        throw std::runtime_error("at least one web host is required");
    }

    std::vector<std::pair<std::string, std::string>> requestedHosts;
    requestedHosts.reserve(config.hosts.size());
    bool hasAnyHost = false;
    for (const auto& host : config.hosts) {
        const auto canonical = canonicalHostAddress(host);
        if (canonical == "0.0.0.0") {
            hasAnyHost = true;
        }
        requestedHosts.push_back({host, canonical});
    }

    std::vector<Listener> listeners;
    listeners.reserve(requestedHosts.size());
    std::set<std::string> boundAddresses;
    for (const auto& [host, canonical] : requestedHosts) {
        if (hasAnyHost && canonical != "0.0.0.0") {
            continue;
        }
        if (!boundAddresses.insert(canonical).second) {
            continue;
        }
        listeners.push_back({createListener(canonical, config.port), host});
        std::cout << "EchoIR web listening on http://" << host << ":" << config.port << "\n";
    }
    if (listeners.empty()) {
        throw std::runtime_error("no web listeners were created");
    }
    std::cout << "Code directory: " << config.codeDirectory << "\n" << std::flush;

    while (true) {
        fd_set readSet;
        FD_ZERO(&readSet);
        SocketHandle maxSocket = 0;
        for (const auto& listener : listeners) {
            FD_SET(listener.socket, &readSet);
            if (listener.socket > maxSocket) {
                maxSocket = listener.socket;
            }
        }

#ifdef _WIN32
        const auto ready = select(0, &readSet, nullptr, nullptr, nullptr);
#else
        const auto ready = select(maxSocket + 1, &readSet, nullptr, nullptr, nullptr);
#endif
        if (ready <= 0) {
            continue;
        }

        for (const auto& listener : listeners) {
            if (!FD_ISSET(listener.socket, &readSet)) {
                continue;
            }
            sockaddr_in clientAddress {};
#ifdef _WIN32
            int clientLength = sizeof(clientAddress);
#else
            socklen_t clientLength = sizeof(clientAddress);
#endif
            const auto client = accept(listener.socket, reinterpret_cast<sockaddr*>(&clientAddress), &clientLength);
            if (client == kInvalidSocket) {
                continue;
            }
            try {
                const auto request = readRequest(client);
                sendResponse(client, handleRequest(request, config, runner));
            } catch (const std::exception& error) {
                try {
                    sendResponse(client, errorResponse(error));
                } catch (const std::exception&) {
                }
            }
            closeSocket(client);
        }
    }
}

}  // namespace echoir
