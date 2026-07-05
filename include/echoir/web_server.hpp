#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace echoir {

struct WebCommandResult {
    int exitCode = 0;
    std::string stdoutText;
};

using WebCommandRunner = std::function<WebCommandResult(const std::string& command,
                                                        const std::map<std::string, std::string>& options)>;

struct WebServerConfig {
    int port = 8787;
    std::vector<std::string> hosts = {"127.0.0.1"};
    std::string serialPort;
    std::vector<std::string> detectedSerialPorts;
    std::string codeDirectory = "codes";
};

void runEmbeddedWebServer(const WebServerConfig& config, const WebCommandRunner& runner);

}  // namespace echoir
