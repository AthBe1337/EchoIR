#pragma once

#include "echoir/ac.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace echoir {

enum class OfficialAcProtocol {
    Gree1,
    Midea1,
    Gree2,
    Tcl1,
    Aux1,
    Haier1,
    Mi1,
    Chigo1,
    Daikin1,
    Chiq1,
    Panasonic,
    Midea2,
    Gree3,
    Mitsubishi1,
    Hisense2,
    Hisense3,
    Hisense1,
    Haier2,
    Haier3,
    Haier4,
    Daikin2,
};

struct OfficialAcState {
    OfficialAcProtocol protocol = OfficialAcProtocol::Midea1;
    std::array<std::uint8_t, 32> fields {};

    std::uint8_t field(int officialOffset) const;
    void setField(int officialOffset, std::uint8_t value);
};

struct OfficialMidea1Options {
    bool power = true;
    AcMode mode = AcMode::Cool;
    int temperature = 24;
    AcFan fan = AcFan::Auto;
    std::uint8_t swingIndex = 0;
    std::uint8_t timerIndex = 0;
};

struct OfficialMidea2Options {
    bool power = true;
    AcMode mode = AcMode::Cool;
    int temperature = 24;
    AcFan fan = AcFan::Auto;
    std::uint8_t swingIndex = 0;
};

const std::vector<OfficialAcProtocol>& officialAcProtocols();
const char* officialAcProtocolName(OfficialAcProtocol protocol);
std::optional<OfficialAcProtocol> parseOfficialAcProtocol(const std::string& text);
OfficialAcState defaultOfficialAcState(OfficialAcProtocol protocol);
std::vector<std::uint32_t> generateOfficialAcDurations(const OfficialAcState& state);
Bytes encodeOfficialAc(const OfficialAcState& state);

std::vector<std::uint32_t> generateMidea1Durations(const OfficialMidea1Options& options);
std::vector<std::uint32_t> generateMidea2Durations(const OfficialMidea2Options& options);
Bytes encodeDurationsAsExternalCode(const std::vector<std::uint32_t>& durations,
                                    std::uint32_t compressRate = 8);
Bytes encodeOfficialMidea1(const OfficialMidea1Options& options);
Bytes encodeOfficialMidea2(const OfficialMidea2Options& options);

}  // namespace echoir
