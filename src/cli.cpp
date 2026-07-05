#include "echoir/ac.hpp"
#include "echoir/code_store.hpp"
#include "echoir/device.hpp"
#include "echoir/serial.hpp"

#include <filesystem>
#include <cctype>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

using echoir::Bytes;

struct ParsedArgs {
    std::map<std::string, std::string> options;
    std::vector<std::string> positionals;

    bool has(const std::string& name) const {
        return options.find(name) != options.end();
    }

    std::string get(const std::string& name, const std::string& fallback = {}) const {
        const auto it = options.find(name);
        return it == options.end() ? fallback : it->second;
    }
};

void usage() {
    std::cout
        << "EchoIR C++ CLI\n"
        << "\n"
        << "Usage:\n"
        << "  echoir info [--port /dev/ttyUSB0] [--baud 115200]\n"
        << "  echoir set-baud --rate 115200\n"
        << "  echoir set-address --new-address 00\n"
        << "  echoir reset | format\n"
        << "  echoir learn-internal --slot 0 [--timeout-ms 15000]\n"
        << "  echoir send-internal --slot 0\n"
        << "  echoir read-internal --slot 0 --out code.json\n"
        << "  echoir write-internal --slot 0 --in code.json\n"
        << "  echoir auto-send --slot 0 --enable 1\n"
        << "  echoir get-auto-send --slot 0\n"
        << "  echoir auto-delay --seconds 120\n"
        << "  echoir get-auto-delay\n"
        << "  echoir learn-external --name tv_power [--out tv_power.json] [--timeout-ms 15000]\n"
        << "  echoir send-external --in tv_power.json\n"
        << "  echoir list [--dir .]\n"
        << "  echoir dump --in tv_power.json\n"
        << "  echoir ac [--brand midea|--brand-code 00] [--power on] [--mode cool] [--temp 26] [--fan mid]\n"
        << "\n"
        << "Common options:\n"
        << "  --port PATH        Serial device, default /dev/ttyUSB0\n"
        << "  --baud RATE        9600/19200/38400/57600/115200, default 115200\n"
        << "  --address HEX      Module address for downlink, default FF broadcast\n"
        << "  --profile high     high supports internal slots 0..95, basic supports 0..6\n"
        << "  --dry-run          For ac command, print frames without opening serial\n";
}

ParsedArgs parseArgs(int argc, char** argv) {
    ParsedArgs parsed;
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind("--", 0) == 0) {
            const std::string key = arg.substr(2);
            if (i + 1 < argc && std::string(argv[i + 1]).rfind("--", 0) != 0) {
                parsed.options[key] = argv[++i];
            } else {
                parsed.options[key] = "true";
            }
        } else {
            parsed.positionals.push_back(arg);
        }
    }
    return parsed;
}

int parseInt(const std::string& value, const std::string& name) {
    try {
        std::size_t consumed = 0;
        const int parsed = std::stoi(value, &consumed, 0);
        if (consumed != value.size()) {
            throw std::invalid_argument("trailing characters");
        }
        return parsed;
    } catch (const std::exception&) {
        throw std::invalid_argument(name + " must be an integer");
    }
}

std::uint8_t parseByte(const std::string& value, const std::string& name) {
    const bool looksHex = value.find("0x") != std::string::npos ||
                          value.find("0X") != std::string::npos ||
                          value.find_first_of("abcdefABCDEF") != std::string::npos;
    if (looksHex) {
        try {
            const auto bytes = echoir::fromHex(value);
            if (bytes.size() == 1) {
                return bytes[0];
            }
        } catch (const std::exception&) {
        }
    }

    if (value.size() == 2 && value[0] == '0' && std::isdigit(static_cast<unsigned char>(value[1]))) {
        const auto bytes = echoir::fromHex(value);
        if (bytes.size() == 1) {
            return bytes[0];
        }
    }

    const int parsed = parseInt(value, name);
    if (parsed < 0 || parsed > 0xFF) {
        throw std::invalid_argument(name + " must fit in one byte");
    }
    return static_cast<std::uint8_t>(parsed);
}

std::uint8_t parseSlot(const ParsedArgs& args) {
    if (!args.has("slot")) {
        throw std::invalid_argument("--slot is required");
    }
    return parseByte(args.get("slot"), "slot");
}

bool parseBool(const std::string& value, const std::string& name) {
    if (value == "1" || value == "true" || value == "on" || value == "yes") {
        return true;
    }
    if (value == "0" || value == "false" || value == "off" || value == "no") {
        return false;
    }
    throw std::invalid_argument(name + " must be true/false or 1/0");
}

std::chrono::milliseconds timeoutFromArgs(const ParsedArgs& args, int fallbackMs) {
    return std::chrono::milliseconds(parseInt(args.get("timeout-ms", std::to_string(fallbackMs)), "timeout-ms"));
}

echoir::ModuleProfile profileFromArgs(const ParsedArgs& args) {
    const auto profile = args.get("profile", "high");
    if (profile == "basic") {
        return echoir::ModuleProfile::Basic;
    }
    if (profile == "high") {
        return echoir::ModuleProfile::High;
    }
    throw std::invalid_argument("--profile must be high or basic");
}

std::unique_ptr<echoir::IrDevice> openDevice(const ParsedArgs& args) {
    const auto port = args.get("port", "/dev/ttyUSB0");
    const int baud = parseInt(args.get("baud", "115200"), "baud");
    const auto address = parseByte(args.get("address", "FF"), "address");
    return std::make_unique<echoir::IrDevice>(echoir::openSerialTransport(port, baud), address, profileFromArgs(args));
}

void printAck(const echoir::Ack& ack) {
    std::cout << (ack.ok() ? "OK" : "REJECTED") << " status=" << static_cast<unsigned int>(ack.status) << "\n";
}

std::string defaultOutputForName(const std::string& name) {
    return name.empty() ? "ir_code.json" : name + ".json";
}

void handleAc(const ParsedArgs& args) {
    struct Command {
        echoir::AcParameter parameter;
        std::uint8_t value;
    };

    std::optional<std::uint8_t> brand;
    std::optional<std::uint8_t> power;
    std::optional<std::uint8_t> mode;
    std::optional<std::uint8_t> temperature;
    std::optional<std::uint8_t> fan;
    std::unordered_map<std::string, std::uint8_t> extraBrands;
    if (args.has("brand-file")) {
        extraBrands = echoir::loadAcBrandFile(args.get("brand-file"));
    }
    if (args.has("brand") && args.has("brand-code")) {
        throw std::invalid_argument("use either --brand or --brand-code, not both");
    }
    if (args.has("brand")) {
        const auto value = echoir::resolveAcBrand(args.get("brand"), extraBrands);
        if (!value) {
            throw std::invalid_argument("unknown AC brand; use --brand-code HEX or --brand-file");
        }
        brand = *value;
    }
    if (args.has("brand-code")) {
        brand = parseByte(args.get("brand-code"), "brand-code");
    }
    if (args.has("power")) {
        power = echoir::encodeAcPower(echoir::parseAcPower(args.get("power")));
    }
    if (args.has("mode")) {
        mode = echoir::encodeAcMode(echoir::parseAcMode(args.get("mode")));
    }
    if (args.has("temp")) {
        temperature = echoir::encodeAcTemperature(parseInt(args.get("temp"), "temp"));
    }
    if (args.has("fan")) {
        fan = echoir::encodeAcFan(echoir::parseAcFan(args.get("fan")));
    }
    if (!brand && !power && !mode && !temperature && !fan) {
        throw std::invalid_argument("ac requires at least one of --brand, --brand-code, --power, --mode, --temp, --fan");
    }

    const auto address = parseByte(args.get("address", "FF"), "address");
    const bool hasFullCommand = brand && power && mode && temperature && fan;
    if (hasFullCommand) {
        const echoir::AcFullCommand full{*brand, *power, *mode, *temperature, *fan};
        const auto frame = echoir::encodeAcFullFrame(full, address);
        if (args.has("dry-run")) {
            std::cout << echoir::toHex(frame) << "\n";
            return;
        }

        auto device = openDevice(args);
        device->sendAcFull(full.brand, full.power, full.mode, full.temperature, full.fan, args.has("wait-ack"));
        std::cout << "sent " << echoir::toHex(frame) << "\n";
        return;
    }

    std::vector<Command> commands;
    if (brand) {
        commands.push_back({echoir::AcParameter::Brand, *brand});
    }
    if (power) {
        commands.push_back({echoir::AcParameter::Power, *power});
    }
    if (mode) {
        commands.push_back({echoir::AcParameter::Mode, *mode});
    }
    if (temperature) {
        commands.push_back({echoir::AcParameter::Temperature, *temperature});
    }
    if (fan) {
        commands.push_back({echoir::AcParameter::Fan, *fan});
    }

    if (args.has("dry-run")) {
        for (const auto& command : commands) {
            std::cout << echoir::toHex(echoir::encodeAcFrame(command.parameter, command.value, address)) << "\n";
        }
        return;
    }

    auto device = openDevice(args);
    const bool waitAck = args.has("wait-ack");
    for (const auto& command : commands) {
        device->sendAcParameter(static_cast<std::uint8_t>(command.parameter), command.value, waitAck);
        std::cout << "sent " << echoir::toHex(echoir::encodeAcFrame(command.parameter, command.value, address)) << "\n";
    }
}

int runCommand(const std::string& command, const ParsedArgs& args) {
    if (command == "help" || command == "--help" || command == "-h") {
        usage();
        return 0;
    }
    if (command == "ac") {
        handleAc(args);
        return 0;
    }
    if (command == "list") {
        const std::filesystem::path dir(args.get("dir", "."));
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                std::cout << entry.path().string() << "\n";
            }
        }
        return 0;
    }
    if (command == "dump") {
        if (!args.has("in")) {
            throw std::invalid_argument("--in is required");
        }
        const auto code = echoir::loadStoredCode(args.get("in"));
        std::cout << "name: " << code.name << "\n"
                  << "created_at: " << code.createdAt << "\n"
                  << "address: " << echoir::toHex({code.address}, false) << "\n"
                  << "afn: " << echoir::toHex({code.afn}, false) << "\n"
                  << "data: " << echoir::toHex(code.data) << "\n";
        if (!code.frame.empty()) {
            std::cout << "frame: " << echoir::toHex(code.frame) << "\n";
        }
        return 0;
    }

    auto device = openDevice(args);

    if (command == "info") {
        const auto baudIndex = device->getBaudIndex();
        std::cout << "baud_index: " << static_cast<unsigned int>(baudIndex)
                  << " (" << echoir::baudRateFromIndex(baudIndex) << ")\n";
        std::cout << "module_address: " << echoir::toHex({device->getModuleAddress()}, false) << "\n";
    } else if (command == "set-baud") {
        if (!args.has("rate")) {
            throw std::invalid_argument("--rate is required");
        }
        printAck(device->setBaudIndex(echoir::baudIndexFromRate(parseInt(args.get("rate"), "rate"))));
    } else if (command == "set-address") {
        if (!args.has("new-address")) {
            throw std::invalid_argument("--new-address is required");
        }
        printAck(device->setModuleAddress(parseByte(args.get("new-address"), "new-address")));
    } else if (command == "reset") {
        printAck(device->reset());
    } else if (command == "format") {
        printAck(device->format());
    } else if (command == "learn-internal") {
        const auto report = device->learnInternal(parseSlot(args), timeoutFromArgs(args, 15000));
        std::cout << (report.ok() ? "OK" : "FAILED")
                  << " flag=" << echoir::toHex({report.flag}, false)
                  << " slot=" << static_cast<unsigned int>(report.slot)
                  << " status=" << static_cast<unsigned int>(report.status) << "\n";
    } else if (command == "exit-internal") {
        printAck(device->exitInternalLearn());
    } else if (command == "send-internal") {
        printAck(device->sendInternal(parseSlot(args)));
    } else if (command == "read-internal") {
        const auto code = device->readInternal(parseSlot(args));
        std::cout << (code.ok() ? "OK" : "FAILED")
                  << " slot=" << static_cast<unsigned int>(code.slot)
                  << " bytes=" << code.code.size() << "\n";
        if (args.has("out")) {
            echoir::StoredCode stored;
            stored.name = args.get("name", "internal_slot_" + std::to_string(code.slot));
            stored.createdAt = echoir::currentTimestamp();
            stored.address = device->address();
            stored.afn = 0x18;
            stored.data = code.code;
            echoir::saveStoredCode(args.get("out"), stored);
            std::cout << "saved " << args.get("out") << "\n";
        } else {
            std::cout << echoir::toHex(code.code) << "\n";
        }
    } else if (command == "write-internal") {
        if (!args.has("in")) {
            throw std::invalid_argument("--in is required");
        }
        const auto code = echoir::loadStoredCode(args.get("in"));
        printAck(device->writeInternal(parseSlot(args), code.data));
    } else if (command == "auto-send") {
        const auto slot = parseSlot(args);
        if (!args.has("enable")) {
            throw std::invalid_argument("--enable is required");
        }
        printAck(device->setPowerOnSend(slot, parseBool(args.get("enable"), "enable")));
    } else if (command == "get-auto-send") {
        std::cout << (device->getPowerOnSend(parseSlot(args)) ? "enabled" : "disabled") << "\n";
    } else if (command == "auto-delay") {
        if (!args.has("seconds")) {
            throw std::invalid_argument("--seconds is required");
        }
        const int seconds = parseInt(args.get("seconds"), "seconds");
        if (seconds < 0 || seconds > 65535) {
            throw std::invalid_argument("--seconds must be 0..65535");
        }
        printAck(device->setPowerOnDelaySeconds(static_cast<std::uint16_t>(seconds)));
    } else if (command == "get-auto-delay") {
        std::cout << device->getPowerOnDelaySeconds() << "\n";
    } else if (command == "learn-external") {
        const auto code = device->learnExternal(timeoutFromArgs(args, 15000));
        echoir::StoredCode stored;
        stored.name = args.get("name", "external_code");
        stored.createdAt = echoir::currentTimestamp();
        stored.address = device->address();
        stored.afn = 0x22;
        stored.data = code;
        stored.frame = echoir::encodeFrame(device->address(), 0x22, code);
        const auto out = args.get("out", defaultOutputForName(stored.name));
        echoir::saveStoredCode(out, stored);
        std::cout << "saved " << out << " bytes=" << code.size() << "\n";
    } else if (command == "exit-external") {
        printAck(device->exitExternalLearn());
    } else if (command == "send-external") {
        if (!args.has("in")) {
            throw std::invalid_argument("--in is required");
        }
        const auto code = echoir::loadStoredCode(args.get("in"));
        printAck(device->sendExternal(code.data));
    } else {
        throw std::invalid_argument("unknown command: " + command);
    }

    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        usage();
        return 2;
    }

    try {
        return runCommand(argv[1], parseArgs(argc, argv));
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n\n";
        usage();
        return 1;
    }
}
