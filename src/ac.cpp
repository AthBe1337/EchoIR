#include "echoir/ac.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace echoir {
namespace {

std::string normalized(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        if (c == '-' || c == ' ') {
            return static_cast<char>('_');
        }
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

std::uint8_t parseByteValue(const std::string& text) {
    std::string value = text;
    value.erase(std::remove(value.begin(), value.end(), '"'), value.end());
    const int base = value.rfind("0x", 0) == 0 || value.rfind("0X", 0) == 0 ? 16 : 10;
    const auto parsed = std::stoul(value, nullptr, base);
    if (parsed > 0xFF) {
        throw std::invalid_argument("AC brand value must fit in one byte");
    }
    return static_cast<std::uint8_t>(parsed);
}

}  // namespace

const std::unordered_map<std::string, std::uint8_t>& builtinAcBrands() {
    static const std::unordered_map<std::string, std::uint8_t> brands = {
        {"gree", 0x00},
        {"gree1", 0x00},
        {"geli", 0x00},
        {"geli1", 0x00},
        {"gree2", 0x01},
        {"geli2", 0x01},
        {"gree3", 0x02},
        {"geli3", 0x02},
        {"midea", 0x10},
        {"midea1", 0x10},
        {"meidi", 0x10},
        {"meidi1", 0x10},
        {"midea2", 0x11},
        {"meidi2", 0x11},
        {"haier", 0x22},
        {"haier1", 0x22},
        {"haier2", 0x23},
        {"haier3", 0x24},
        {"haier4", 0x20},
        {"tcl", 0x30},
        {"aux", 0x40},
        {"aokesi", 0x40},
        {"xiaomi", 0x50},
        {"mi", 0x50},
        {"hisense", 0x60},
        {"hisense1", 0x60},
        {"haixin", 0x60},
        {"haixin1", 0x60},
        {"hisense2", 0x61},
        {"haixin2", 0x61},
        {"hisense3", 0x62},
        {"haixin3", 0x62},
        {"chigo", 0x70},
        {"zhigao", 0x70},
        {"changhong", 0x80},
        {"daikin", 0x90},
        {"daikin1", 0x90},
        {"dajin", 0x90},
        {"dajin1", 0x90},
        {"daikin2", 0x91},
        {"dajin2", 0x91},
        {"panasonic", 0xA0},
        {"songxia", 0xA0},
        {"mitsubishi", 0xB0},
        {"sanling", 0xB0},
    };
    return brands;
}

std::unordered_map<std::string, std::uint8_t> loadAcBrandFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("failed to open AC brand file: " + path);
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    const std::string text = buffer.str();

    std::unordered_map<std::string, std::uint8_t> brands;
    const std::regex item("\"([^\"]+)\"\\s*:\\s*(\"?0[xX][0-9a-fA-F]+\"?|\"?[0-9]+\"?)");
    for (std::sregex_iterator it(text.begin(), text.end(), item), end; it != end; ++it) {
        const std::string key = normalized((*it)[1].str());
        if (key == "brands") {
            continue;
        }
        brands[key] = parseByteValue((*it)[2].str());
    }
    return brands;
}

std::optional<std::uint8_t> resolveAcBrand(const std::string& name,
                                           const std::unordered_map<std::string, std::uint8_t>& extraBrands) {
    const auto key = normalized(name);
    const auto extra = extraBrands.find(key);
    if (extra != extraBrands.end()) {
        return extra->second;
    }
    const auto builtin = builtinAcBrands().find(key);
    if (builtin != builtinAcBrands().end()) {
        return builtin->second;
    }
    return std::nullopt;
}

std::uint8_t encodeAcPower(AcPower power) {
    return power == AcPower::On ? 0x01 : 0x00;
}

std::uint8_t encodeAcMode(AcMode mode) {
    switch (mode) {
        case AcMode::Auto:
            return 0x00;
        case AcMode::Cool:
            return 0x01;
        case AcMode::Dry:
            return 0x02;
        case AcMode::Fan:
            return 0x03;
        case AcMode::Heat:
            return 0x04;
    }
    throw std::invalid_argument("invalid AC mode");
}

std::uint8_t encodeAcTemperature(int celsius) {
    if (celsius < 16 || celsius > 30) {
        throw std::invalid_argument("temperature must be 16..30 Celsius");
    }
    return static_cast<std::uint8_t>(celsius);
}

std::uint8_t encodeAcFan(AcFan fan) {
    switch (fan) {
        case AcFan::Auto:
            return 0x00;
        case AcFan::Low:
            return 0x01;
        case AcFan::Medium:
            return 0x02;
        case AcFan::High:
            return 0x03;
    }
    throw std::invalid_argument("invalid AC fan setting");
}

AcPower parseAcPower(const std::string& text) {
    const auto value = normalized(text);
    if (value == "on" || value == "1") {
        return AcPower::On;
    }
    if (value == "off" || value == "0") {
        return AcPower::Off;
    }
    throw std::invalid_argument("power must be on/off");
}

AcMode parseAcMode(const std::string& text) {
    const auto value = normalized(text);
    if (value == "auto") {
        return AcMode::Auto;
    }
    if (value == "cool") {
        return AcMode::Cool;
    }
    if (value == "dry" || value == "dehumidify") {
        return AcMode::Dry;
    }
    if (value == "fan") {
        return AcMode::Fan;
    }
    if (value == "heat") {
        return AcMode::Heat;
    }
    throw std::invalid_argument("mode must be auto/cool/dry/fan/heat");
}

AcFan parseAcFan(const std::string& text) {
    const auto value = normalized(text);
    if (value == "auto") {
        return AcFan::Auto;
    }
    if (value == "low") {
        return AcFan::Low;
    }
    if (value == "mid" || value == "medium") {
        return AcFan::Medium;
    }
    if (value == "high") {
        return AcFan::High;
    }
    throw std::invalid_argument("fan must be auto/low/mid/high");
}

Bytes encodeAcFrame(AcParameter parameter, std::uint8_t value, std::uint8_t address) {
    return encodeFrame(address,
                       0x31,
                       {static_cast<std::uint8_t>(parameter), value},
                       ChecksumMode::Fixed55);
}

Bytes encodeAcFullFrame(const AcFullCommand& command, std::uint8_t address) {
    return encodeFrame(address,
                       0x30,
                       {command.brand, command.power, command.mode, command.temperature, command.fan},
                       ChecksumMode::Fixed55);
}

}  // namespace echoir
