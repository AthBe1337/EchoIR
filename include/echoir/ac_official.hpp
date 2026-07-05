#pragma once

#include "echoir/ac.hpp"

#include <cstdint>
#include <vector>

namespace echoir {

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

std::vector<std::uint32_t> generateMidea1Durations(const OfficialMidea1Options& options);
std::vector<std::uint32_t> generateMidea2Durations(const OfficialMidea2Options& options);
Bytes encodeDurationsAsExternalCode(const std::vector<std::uint32_t>& durations,
                                    std::uint32_t compressRate = 8);
Bytes encodeOfficialMidea1(const OfficialMidea1Options& options);
Bytes encodeOfficialMidea2(const OfficialMidea2Options& options);

}  // namespace echoir
