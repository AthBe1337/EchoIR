#pragma once

#include "echoir/frame.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace echoir {

enum class AcParameter : std::uint8_t {
    Brand = 0x00,
    Power = 0x01,
    Mode = 0x02,
    Temperature = 0x03,
    Fan = 0x04,
};

enum class AcPower {
    Off,
    On,
};

enum class AcMode {
    Auto,
    Cool,
    Dry,
    Fan,
    Heat,
};

enum class AcFan {
    Auto,
    Low,
    Medium,
    High,
};

struct AcParameterCommand {
    AcParameter parameter;
    std::uint8_t value;
};

struct AcFullCommand {
    std::uint8_t brand = 0;
    std::uint8_t power = 0;
    std::uint8_t mode = 0;
    std::uint8_t temperature = 0;
    std::uint8_t fan = 0;
};

const std::unordered_map<std::string, std::uint8_t>& builtinAcBrands();
std::unordered_map<std::string, std::uint8_t> loadAcBrandFile(const std::string& path);
std::optional<std::uint8_t> resolveAcBrand(const std::string& name,
                                           const std::unordered_map<std::string, std::uint8_t>& extraBrands = {});

std::uint8_t encodeAcPower(AcPower power);
std::uint8_t encodeAcMode(AcMode mode);
std::uint8_t encodeAcTemperature(int celsius);
std::uint8_t encodeAcFan(AcFan fan);

AcPower parseAcPower(const std::string& text);
AcMode parseAcMode(const std::string& text);
AcFan parseAcFan(const std::string& text);

Bytes encodeAcFrame(AcParameter parameter, std::uint8_t value, std::uint8_t address = 0xFF);
Bytes encodeAcFullFrame(const AcFullCommand& command, std::uint8_t address = 0xFF);

}  // namespace echoir
