#include "echoir/ac_official.hpp"

#include <array>
#include <cstddef>
#include <stdexcept>

namespace echoir {
namespace {

constexpr std::size_t kOfficialExternalMaxBytes = 0x310;
constexpr std::array<std::uint8_t, 5> kMideaModeMap = {2, 0, 1, 1, 3};
constexpr std::array<std::uint8_t, 14> kMideaTempMap = {0, 1, 3, 2, 6, 7, 5, 4, 12, 13, 9, 8, 10, 11};
constexpr std::array<std::uint8_t, 4> kMideaFanPowerMap = {5, 4, 2, 1};
constexpr std::array<std::uint8_t, 4> kMidea1FanTail = {102, 40, 60, 100};

void appendBits(std::vector<std::uint32_t>& durations,
                std::uint8_t value,
                int bits,
                bool msbFirst,
                std::uint32_t zeroMark,
                std::uint32_t zeroSpace,
                std::uint32_t oneMark,
                std::uint32_t oneSpace) {
    if (bits < 0 || bits > 8) {
        throw std::invalid_argument("bit count must be 0..8");
    }
    for (int i = 0; i < bits; ++i) {
        const int bit = msbFirst ? (bits - 1 - i) : i;
        const bool one = ((value >> bit) & 0x01U) != 0;
        durations.push_back(one ? oneMark : zeroMark);
        durations.push_back(one ? oneSpace : zeroSpace);
    }
}

void appendMideaByte(std::vector<std::uint32_t>& durations, std::uint8_t value) {
    appendBits(durations, value, 8, true, 540, 540, 540, 1620);
}

std::uint8_t officialModeIndex(AcMode mode) {
    switch (mode) {
        case AcMode::Auto:
            return 0;
        case AcMode::Cool:
            return 1;
        case AcMode::Dry:
            return 2;
        case AcMode::Fan:
            return 3;
        case AcMode::Heat:
            return 4;
    }
    throw std::invalid_argument("invalid AC mode");
}

std::uint8_t officialFanIndex(AcFan fan) {
    switch (fan) {
        case AcFan::Auto:
            return 0;
        case AcFan::Low:
            return 1;
        case AcFan::Medium:
            return 2;
        case AcFan::High:
            return 3;
    }
    throw std::invalid_argument("invalid AC fan");
}

void appendVarint(Bytes& out, std::uint32_t value) {
    if (value <= 0x7F) {
        out.push_back(static_cast<std::uint8_t>(value));
    } else if (value <= 0x3FFF) {
        out.push_back(static_cast<std::uint8_t>((value & 0x7FU) | 0x80U));
        out.push_back(static_cast<std::uint8_t>((value >> 7U) & 0x7FU));
    } else {
        out.push_back(static_cast<std::uint8_t>((value & 0x7FU) | 0x80U));
        out.push_back(static_cast<std::uint8_t>(((value >> 7U) & 0x7FU) | 0x80U));
        out.push_back(static_cast<std::uint8_t>((value >> 14U) & 0x7FU));
    }
}

std::uint8_t byteSum(const std::uint8_t* bytes, std::size_t count) {
    unsigned int sum = 0;
    for (std::size_t i = 0; i < count; ++i) {
        sum += bytes[i];
    }
    return static_cast<std::uint8_t>(sum & 0xFFU);
}

void validateMideaTemperature(int temperature) {
    if (temperature < 17 || temperature > 30) {
        throw std::invalid_argument("Midea temperature must be 17..30 Celsius");
    }
}

std::array<std::uint8_t, 6> buildMideaHeader(bool power,
                                             AcMode optionMode,
                                             int temperature,
                                             AcFan optionFan,
                                             std::uint8_t swingIndex,
                                             std::uint8_t swingScale) {
    validateMideaTemperature(temperature);
    const auto mode = officialModeIndex(optionMode);
    const auto fan = officialFanIndex(optionFan);
    const int tempIndex = temperature - 17;

    std::array<std::uint8_t, 6> bytes {};
    bytes[0] = 0xB2;
    bytes[1] = 0x4D;

    if (!power) {
        bytes[2] = 0x7B;
        bytes[3] = 0x84;
        bytes[4] = 0xE0;
        bytes[5] = 0x1F;
        return bytes;
    }

    std::uint8_t byte2 = 0;
    std::uint8_t tempNibble = 0;
    if ((mode & 0xFDU) != 0) {
        byte2 = static_cast<std::uint8_t>(32U * kMideaFanPowerMap[fan]);
        if (mode != 3) {
            tempNibble = static_cast<std::uint8_t>(16U * kMideaTempMap[tempIndex]);
        } else {
            tempNibble = 0xE0;
        }
    } else {
        tempNibble = static_cast<std::uint8_t>(16U * kMideaTempMap[tempIndex]);
    }

    const auto mappedMode = kMideaModeMap[mode];
    const auto byte4 = static_cast<std::uint8_t>((4U * mappedMode) | tempNibble);
    bytes[4] = byte4;
    if (swingIndex != 0) {
        const auto swing = static_cast<unsigned int>(swingScale) * swingIndex - 1U;
        if (swing > 0x7FU) {
            throw std::invalid_argument("Midea swing index is out of protocol range");
        }
        bytes[4] = static_cast<std::uint8_t>(((swing >> 5U) & 0x03U) | byte4);
        bytes[2] = static_cast<std::uint8_t>((swing & 0x1FU) | byte2);
        bytes[5] = tempIndex < 9 ? 0x7F : 0xFF;
    } else {
        bytes[2] = static_cast<std::uint8_t>(byte2 | 0x1FU);
        bytes[5] = static_cast<std::uint8_t>(~byte4);
    }

    bytes[3] = static_cast<std::uint8_t>(~bytes[2]);
    return bytes;
}

void appendMideaLeader(std::vector<std::uint32_t>& durations) {
    durations.push_back(4400);
    durations.push_back(4400);
}

template <typename ByteContainer>
void appendMideaBytes(std::vector<std::uint32_t>& durations, const ByteContainer& bytes) {
    for (const auto byte : bytes) {
        appendMideaByte(durations, byte);
    }
}

void appendMideaRepeatGapAndLeader(std::vector<std::uint32_t>& durations) {
    durations.push_back(540);
    durations.push_back(5220);
    appendMideaLeader(durations);
}

}  // namespace

std::vector<std::uint32_t> generateMidea1Durations(const OfficialMidea1Options& options) {
    const auto fan = officialFanIndex(options.fan);
    const auto header = buildMideaHeader(options.power,
                                         options.mode,
                                         options.temperature,
                                         options.fan,
                                         options.swingIndex,
                                         2);

    std::vector<std::uint32_t> durations;
    durations.reserve(options.power ? 300 : 200);
    appendMideaLeader(durations);
    appendMideaBytes(durations, header);
    appendMideaRepeatGapAndLeader(durations);
    appendMideaBytes(durations, header);
    if (options.power) {
        std::array<std::uint8_t, 6> trailer {
            0xD5,
            kMidea1FanTail[fan],
            static_cast<std::uint8_t>(32U * options.timerIndex),
            0x00,
            0x00,
            0x00,
        };
        trailer[5] = byteSum(trailer.data(), trailer.size());
        appendMideaRepeatGapAndLeader(durations);
        appendMideaBytes(durations, trailer);
    }
    durations.push_back(540);
    return durations;
}

std::vector<std::uint32_t> generateMidea2Durations(const OfficialMidea2Options& options) {
    const auto header = buildMideaHeader(options.power,
                                         options.mode,
                                         options.temperature,
                                         options.fan,
                                         options.swingIndex,
                                         4);

    std::vector<std::uint32_t> durations;
    durations.reserve(200);
    appendMideaLeader(durations);
    appendMideaBytes(durations, header);
    appendMideaRepeatGapAndLeader(durations);
    appendMideaBytes(durations, header);
    durations.push_back(540);
    return durations;
}

Bytes encodeDurationsAsExternalCode(const std::vector<std::uint32_t>& durations,
                                    std::uint32_t compressRate) {
    if (compressRate == 0) {
        throw std::invalid_argument("compress rate must not be zero");
    }
    Bytes code;
    code.reserve(durations.size() * 2);
    for (const auto duration : durations) {
        const auto compressed = duration / compressRate;
        if (compressed == 0) {
            throw std::invalid_argument("duration is too short for compression rate");
        }
        appendVarint(code, compressed);
    }
    if (code.size() > kOfficialExternalMaxBytes) {
        throw std::invalid_argument("official external code exceeds 0x310-byte module payload limit");
    }
    return code;
}

Bytes encodeOfficialMidea1(const OfficialMidea1Options& options) {
    return encodeDurationsAsExternalCode(generateMidea1Durations(options));
}

Bytes encodeOfficialMidea2(const OfficialMidea2Options& options) {
    return encodeDurationsAsExternalCode(generateMidea2Durations(options));
}

}  // namespace echoir
