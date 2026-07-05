#include "echoir/ac_official.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <stdexcept>
#include <string>

namespace echoir {
namespace {

constexpr std::size_t kOfficialExternalMaxBytes = 0x310;
constexpr std::array<std::uint8_t, 5> kMideaModeMap = {2, 0, 1, 1, 3};
constexpr std::array<std::uint8_t, 14> kMideaTempMap = {0, 1, 3, 2, 6, 7, 5, 4, 12, 13, 9, 8, 10, 11};
constexpr std::array<std::uint8_t, 4> kMideaFanPowerMap = {5, 4, 2, 1};
constexpr std::array<std::uint8_t, 4> kMidea1FanTail = {102, 40, 60, 100};

std::string normalizedProtocol(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        if (c == '-' || c == '_' || c == ' ') {
            return '\0';
        }
        return static_cast<char>(std::tolower(c));
    });
    text.erase(std::remove(text.begin(), text.end(), '\0'), text.end());
    return text;
}

std::uint8_t byteXor(const std::uint8_t* bytes, std::size_t count) {
    std::uint8_t out = 0;
    for (std::size_t i = 0; i < count; ++i) {
        out ^= bytes[i];
    }
    return out;
}

std::uint8_t bitNot(std::uint8_t value) {
    return static_cast<std::uint8_t>(~value);
}

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

class DurationBuilder {
public:
    void setTimings(std::uint32_t zeroMark,
                    std::uint32_t zeroSpace,
                    std::uint32_t oneMark,
                    std::uint32_t oneSpace) {
        zeroMark_ = zeroMark;
        zeroSpace_ = zeroSpace;
        oneMark_ = oneMark;
        oneSpace_ = oneSpace;
    }

    void add(std::uint32_t duration) {
        out.push_back(duration);
    }

    void leader(std::uint32_t mark, std::uint32_t space) {
        add(mark);
        add(space);
    }

    void bits(std::uint8_t value, int count, bool msbFirst) {
        appendBits(out, value, count, msbFirst, zeroMark_, zeroSpace_, oneMark_, oneSpace_);
    }

    std::vector<std::uint32_t> out;

private:
    std::uint32_t zeroMark_ = 0;
    std::uint32_t zeroSpace_ = 0;
    std::uint32_t oneMark_ = 0;
    std::uint32_t oneSpace_ = 0;
};

std::uint8_t f(const OfficialAcState& state, int offset) {
    return state.field(offset);
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
        byte2 = static_cast<std::uint8_t>(32U * kMideaFanPowerMap.at(fan));
        if (mode != 3) {
            tempNibble = static_cast<std::uint8_t>(16U * kMideaTempMap.at(tempIndex));
        } else {
            tempNibble = 0xE0;
        }
    } else {
        tempNibble = static_cast<std::uint8_t>(16U * kMideaTempMap.at(tempIndex));
    }

    const auto mappedMode = kMideaModeMap.at(mode);
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

void appendBytes(DurationBuilder& b, const Bytes& bytes, bool msbFirst = false) {
    for (const auto byte : bytes) {
        b.bits(byte, 8, msbFirst);
    }
}

std::uint8_t greeTimerOnes(std::uint8_t timer) {
    return static_cast<std::uint8_t>((timer >> 1U) % 10U);
}

void appendGreeMainPacket(DurationBuilder& b, const OfficialAcState& state, std::uint8_t marker) {
    const auto timer = f(state, 38);
    const auto timerOnes = greeTimerOnes(timer);
    b.bits(f(state, 32), 3, false);
    b.bits(f(state, 33), 1, false);
    b.bits(f(state, 34), 2, false);
    b.bits(f(state, 35), 1, false);
    b.bits(f(state, 36), 1, false);
    b.bits(f(state, 37), 4, false);
    b.bits(timer & 1U, 1, false);
    b.bits(timer / 20U, 2, false);
    b.bits(timer != 0 ? 1 : 0, 1, false);
    b.bits(timerOnes, 4, false);
    b.bits(f(state, 39), 1, false);
    b.bits(f(state, 40), 1, false);
    b.bits(f(state, 41), 1, false);
    b.bits(f(state, 42), 1, false);
    b.bits(f(state, 43), 1, false);
    b.bits(marker, 7, true);
    b.bits(2, 3, true);
}

void appendGreeFeaturePacket(DurationBuilder& b, const OfficialAcState& state) {
    b.bits(f(state, 44), 1, false);
    b.bits(0, 3, true);
    b.bits(f(state, 45), 1, false);
    b.bits(0, 3, true);
    b.bits(f(state, 46), 2, false);
    b.bits(4, 6, true);
    b.bits(0, 8, true);
    b.bits(0, 4, true);
    const auto checksum = static_cast<std::uint8_t>(
        (f(state, 32) + f(state, 37) + f(state, 45) + 12U + 8U * f(state, 33) +
         greeTimerOnes(f(state, 38))) &
        0x0FU);
    b.bits(checksum, 4, false);
}

void appendGreeEmptyFeaturePacket(DurationBuilder& b, const OfficialAcState& state) {
    b.bits(0, 8, true);
    b.bits(0, 8, true);
    b.bits(0, 4, true);
    auto mode = f(state, 34);
    if (mode > 2) {
        mode = 5;
    }
    b.bits(mode, 4, false);
    b.bits(0, 4, true);
    const auto checksum = static_cast<std::uint8_t>(
        (f(state, 32) + f(state, 37) + 8U * f(state, 33) + 10U + mode) & 0x0FU);
    b.bits(checksum, 4, false);
}

std::vector<std::uint32_t> generateGree2Durations(const OfficialAcState& state) {
    DurationBuilder b;
    b.setTimings(600, 600, 600, 1600);
    b.leader(9000, 4500);
    appendGreeMainPacket(b, state, 0x0A);
    b.add(600);
    b.add(20000);
    appendGreeFeaturePacket(b, state);
    b.add(600);
    return b.out;
}

std::vector<std::uint32_t> generateGree1Durations(const OfficialAcState& state) {
    DurationBuilder b;
    b.setTimings(600, 600, 600, 1600);
    b.leader(9000, 4500);
    appendGreeMainPacket(b, state, 0x0A);
    b.add(600);
    b.add(20000);
    appendGreeFeaturePacket(b, state);
    b.add(600);
    b.add(40000);
    b.leader(9000, 4500);
    appendGreeMainPacket(b, state, 0x0E);
    b.add(600);
    b.add(20000);
    appendGreeEmptyFeaturePacket(b, state);
    b.add(600);
    return b.out;
}

std::vector<std::uint32_t> generateGree3Durations(OfficialAcState state) {
    if (f(state, 33) == 0) {
        state.setField(40, 0);
        state.setField(37, 0);
    } else if (f(state, 32) != 0 && f(state, 32) != 3) {
        state.setField(37, f(state, 37));
    } else {
        state.setField(37, 0);
    }

    const auto timer = f(state, 40);
    DurationBuilder b;
    b.setTimings(600, 600, 600, 1600);
    b.leader(9000, 4500);
    b.bits(f(state, 32), 3, false);
    b.bits(f(state, 33), 1, false);
    b.bits(f(state, 34), 2, false);
    b.bits(f(state, 35), 1, false);
    b.bits(f(state, 37), 1, false);
    b.bits(f(state, 36), 4, false);
    b.bits(timer & 1U, 1, false);
    b.bits(timer / 20U, 2, false);
    b.bits(timer != 0 ? 1 : 0, 1, false);
    b.bits(greeTimerOnes(timer), 4, false);
    b.bits(0, 1, false);
    b.bits(f(state, 38), 1, false);
    b.bits(0, 1, false);
    b.bits(0, 1, false);
    b.bits(f(state, 39), 2, false);
    b.bits(0x0A, 6, true);
    b.bits(2, 3, true);
    b.add(600);
    return b.out;
}

std::vector<std::uint32_t> generatePanasonicDurations(const OfficialAcState& state) {
    static constexpr std::array<std::uint8_t, 4> modeMap = {0, 3, 2, 4};
    static constexpr std::array<std::uint8_t, 4> fanMap = {10, 3, 5, 7};
    static constexpr std::array<std::uint8_t, 6> swingMap = {15, 1, 2, 3, 4, 5};

    Bytes first = {0x02, 0x20, 0xE0, 0x04, 0x00};
    first[4] = byteSum(first.data(), 4);

    Bytes second(19, 0);
    second[0] = 0x02;
    second[1] = 0x20;
    second[2] = 0xE0;
    second[3] = 0x04;
    second[4] = 0x00;
    second[5] = 0x00;
    second[6] = static_cast<std::uint8_t>((16U * modeMap.at(f(state, 32))) | (f(state, 33) & 1U));
    second[7] = static_cast<std::uint8_t>(2U * f(state, 35) + 32U);
    second[8] = static_cast<std::uint8_t>(swingMap.at(f(state, 36)) | (16U * fanMap.at(f(state, 34))));
    second[9] = 0x00;
    second[10] = 0x80;
    second[11] = static_cast<std::uint8_t>(f(state, 37) & 1U);
    second[12] = 0x00;
    second[13] = 0x00;
    second[14] = 0x06;
    second[15] = 0x60;
    second[16] = 0x85;
    second[17] = 0x00;
    second[18] = byteSum(second.data(), 18);

    DurationBuilder b;
    b.setTimings(460, 400, 460, 1260);
    b.leader(3500, 1750);
    appendBytes(b, first);
    b.add(460);
    b.add(10400);
    b.leader(3500, 1750);
    appendBytes(b, second);
    b.add(460);
    return b.out;
}

std::vector<std::uint32_t> generateMitsubishi1Durations(const OfficialAcState& state) {
    static constexpr std::array<std::uint8_t, 5> modeMap = {0, 3, 2, 7, 1};
    static constexpr std::array<std::uint8_t, 5> modeTail = {6, 6, 2, 0, 0};
    static constexpr std::array<std::uint8_t, 4> fanMap = {0, 1, 2, 4};
    static constexpr std::array<std::uint8_t, 6> swingMap = {1, 2, 3, 4, 5, 12};
    static constexpr std::array<std::uint8_t, 6> vaneMap = {0, 1, 2, 3, 4, 7};

    const auto mode = f(state, 33);
    const auto fan = f(state, 35);
    const auto vane = f(state, 37);
    Bytes bytes(18, 0);
    bytes[0] = 0x23;
    bytes[1] = 0xCB;
    bytes[2] = 0x26;
    bytes[3] = 0x01;
    bytes[5] = static_cast<std::uint8_t>(32U * f(state, 32));
    bytes[6] = static_cast<std::uint8_t>(8U * modeMap.at(mode));
    if ((mode < 2 || mode > 3) && mode != 0) {
        bytes[7] = f(state, 34);
    } else {
        bytes[7] = 8;
    }
    bytes[8] = static_cast<std::uint8_t>(modeTail.at(mode) | (16U * swingMap.at(f(state, 38))));
    std::uint8_t highBit = 0x80;
    if (fan != 0) {
        highBit = vane == 0 ? 0x80 : 0x00;
    }
    bytes[9] = static_cast<std::uint8_t>((8U * vaneMap.at(vane)) | fanMap.at(fan) | highBit);
    bytes[14] = static_cast<std::uint8_t>(f(state, 36) << 6U);
    bytes[17] = byteSum(bytes.data(), 17);

    DurationBuilder b;
    b.setTimings(420, 420, 420, 1260);
    b.leader(3450, 1700);
    appendBytes(b, bytes);
    b.add(420);
    b.add(15600);
    b.leader(3450, 1700);
    appendBytes(b, bytes);
    b.add(420);
    return b.out;
}

std::vector<std::uint32_t> generateDaikin1Durations(const OfficialAcState& state) {
    static constexpr std::array<std::uint8_t, 5> modeMap = {0, 3, 2, 6, 4};
    static constexpr std::array<std::uint8_t, 7> fanMap = {10, 11, 3, 4, 5, 6, 7};

    Bytes memory(27, 0);
    memory[0] = 0x11;
    memory[1] = 0xDA;
    memory[2] = 0x27;
    memory[3] = 0xF0;
    memory[7] = byteSum(memory.data(), 7);

    memory[8] = 0x11;
    memory[9] = 0xDA;
    memory[10] = 0x27;
    memory[13] = static_cast<std::uint8_t>((16U * modeMap.at(f(state, 33))) | (f(state, 32) & 1U));
    memory[14] = static_cast<std::uint8_t>(2U * f(state, 34) + 32U);
    memory[16] = static_cast<std::uint8_t>(16U * fanMap.at(f(state, 35)));
    memory[21] = static_cast<std::uint8_t>(f(state, 36) & 1U);
    memory[23] = 0xC0;
    memory[26] = byteSum(memory.data() + 8, 18);

    DurationBuilder b;
    b.setTimings(430, 420, 430, 1320);
    b.leader(3400, 1750);
    appendBytes(b, Bytes(memory.begin(), memory.begin() + 8));
    b.add(430);
    b.add(30000);
    b.leader(3400, 1750);
    appendBytes(b, Bytes(memory.begin() + 8, memory.end()));
    b.add(430);
    return b.out;
}

std::uint8_t nibbleSum(const Bytes& bytes, std::size_t count) {
    unsigned int sum = 0;
    for (std::size_t i = 0; i < count; ++i) {
        sum += (bytes[i] & 0x0FU) + (bytes[i] >> 4U);
    }
    return static_cast<std::uint8_t>(sum & 0xFFU);
}

std::vector<std::uint32_t> generateChiq1Durations(const OfficialAcState& state) {
    static constexpr std::array<std::uint8_t, 2> powerMap = {3, 0};
    static constexpr std::array<std::uint8_t, 3> modeMap = {2, 3, 1};
    static constexpr std::array<std::uint8_t, 4> fanMap = {0, 2, 3, 1};

    const auto mode = f(state, 33);
    const auto timer = f(state, 39);
    std::uint8_t timerHigh = 0;
    std::uint8_t fan = f(state, 35);
    if (mode == 2) {
        timerHigh = static_cast<std::uint8_t>(f(state, 40) << 6U);
    }
    if (timer != 0) {
        fan = 1;
    }
    const auto mappedFan = timer != 0 ? 2 : fanMap.at(fan);

    Bytes bytes(15, 0);
    bytes[0] = 0x56;
    bytes[1] = static_cast<std::uint8_t>(f(state, 34) + 108U);
    bytes[3] = static_cast<std::uint8_t>(timer | (16U * f(state, 41)));
    bytes[4] = static_cast<std::uint8_t>(mappedFan | (16U * modeMap.at(mode)));
    bytes[5] = static_cast<std::uint8_t>(f(state, 38) | (2U * f(state, 37)) | (powerMap.at(f(state, 32)) << 6U));
    bytes[6] = static_cast<std::uint8_t>(timerHigh | (2U * f(state, 43)) | (8U * f(state, 36)));
    bytes[8] = static_cast<std::uint8_t>(f(state, 42) << 7U);
    bytes[10] = f(state, 44);
    bytes[14] = nibbleSum(bytes, 14);

    DurationBuilder b;
    b.setTimings(520, 550, 520, 1620);
    b.leader(8400, 4200);
    appendBytes(b, bytes);
    b.add(520);
    return b.out;
}

std::vector<std::uint32_t> generateAux1Durations(const OfficialAcState& state) {
    static constexpr std::array<std::uint8_t, 5> modeMap = {0, 2, 4, 12, 8};
    static constexpr std::array<std::uint8_t, 2> bitMap = {7, 0};
    static constexpr std::array<std::uint8_t, 4> fanMap = {5, 3, 2, 1};

    const auto mode = f(state, 33);
    const auto power = f(state, 32);
    std::uint8_t timerHigh = 0;
    std::uint8_t timerValue = f(state, 40);
    std::uint8_t timerPowerBit = 0;
    std::uint8_t timerPowerHigh = 0;
    std::uint8_t timerMode = 0;
    if (mode == 1 || mode == 4) {
        timerMode = f(state, 39);
        timerHigh = static_cast<std::uint8_t>(timerMode << 6U);
    } else {
        timerMode = 0;
    }
    if (timerValue != 0) {
        timerPowerBit = power != 0 ? 0x40 : 0x00;
        timerPowerHigh = power == 0 ? 0x80 : 0x00;
    }

    std::uint8_t fanValue = 0;
    if (timerMode != 0) {
        fanValue = 32;
    } else {
        fanValue = static_cast<std::uint8_t>(32U * fanMap.at(f(state, 35)));
    }

    Bytes bytes(13, 0);
    bytes[0] = 0xC3;
    bytes[1] = static_cast<std::uint8_t>(bitMap.at(f(state, 37)) | (8U * f(state, 34) + 64U));
    bytes[2] = static_cast<std::uint8_t>(32U * bitMap.at(f(state, 38)));
    bytes[4] = static_cast<std::uint8_t>(timerValue | fanValue);
    bytes[5] = static_cast<std::uint8_t>(timerHigh | (f(state, 44) << 7U));
    bytes[6] = static_cast<std::uint8_t>((16U * modeMap.at(mode)) | f(state, 45) | (4U * f(state, 36)));
    bytes[8] = static_cast<std::uint8_t>(f(state, 46) << 7U);
    bytes[9] = static_cast<std::uint8_t>(
        (32U * power) | timerPowerHigh | (2U * f(state, 41)) | (16U * f(state, 42)) |
        (8U * f(state, 43)) | timerPowerBit);
    bytes[11] = 5;
    bytes[12] = byteSum(bytes.data(), 12);

    DurationBuilder b;
    b.setTimings(560, 560, 560, 1680);
    b.leader(9000, 4500);
    appendBytes(b, bytes);
    b.add(560);
    return b.out;
}

std::vector<std::uint32_t> generateTcl1Durations(const OfficialAcState& state) {
    static constexpr std::array<std::uint8_t, 5> modeMap = {8, 3, 2, 7, 1};
    static constexpr std::array<std::uint8_t, 4> fanMap = {0, 2, 3, 5};
    static constexpr std::array<std::uint8_t, 2> bitMap = {0, 7};
    static constexpr std::array<std::uint8_t, 2> toggleMap = {1, 0};
    static constexpr std::array<std::uint8_t, 4> firstModeMap = {2, 4, 8, 12};
    static constexpr std::array<std::uint8_t, 2> swingMap = {0, 9};

    std::uint8_t timerByte = 0;
    std::uint8_t timerFanBit = 0;
    std::uint8_t timerLeftActive = 0;
    std::uint8_t timerRightActive = 0;
    std::uint8_t timerValue = 0;
    if (f(state, 43) != 0) {
        timerValue = static_cast<std::uint8_t>(3U * f(state, 43));
        timerByte = 16;
        timerRightActive = 1;
    } else if (f(state, 44) != 0) {
        timerValue = static_cast<std::uint8_t>(3U * f(state, 44));
        timerLeftActive = 1;
        timerFanBit = 8;
    }

    Bytes first(14, 0);
    first[0] = 0x23;
    first[1] = 0xCB;
    first[2] = 0x26;
    first[3] = 0x02;
    first[5] = 0x40;
    first[6] = static_cast<std::uint8_t>(16U * firstModeMap.at(f(state, 35)));
    first[7] = static_cast<std::uint8_t>((16U * swingMap.at(f(state, 38))) | (8U * f(state, 37)));
    first[8] = 0x83;
    first[13] = static_cast<std::uint8_t>(byteSum(first.data(), 13) + 15U);

    Bytes second(14, 0);
    second[0] = 0x23;
    second[1] = 0xCB;
    second[2] = 0x26;
    second[3] = 0x01;
    second[4] = 0x01;
    second[5] = static_cast<std::uint8_t>(timerByte | (4U * f(state, 32)) | (f(state, 36) << 6U) |
                                          (f(state, 39) << 7U) | (32U * toggleMap.at(f(state, 40))) |
                                          timerFanBit);
    second[6] = modeMap.at(f(state, 33));
    second[7] = static_cast<std::uint8_t>(15U - f(state, 34));
    second[8] = static_cast<std::uint8_t>(fanMap.at(f(state, 35)) | (8U * bitMap.at(f(state, 37))));
    second[9] = timerLeftActive ? timerValue : 0;
    second[10] = timerRightActive ? timerValue : 0;
    second[12] = static_cast<std::uint8_t>((toggleMap.at(f(state, 42)) << 7U) |
                                           (32U * f(state, 41)) |
                                           (8U * f(state, 38)));
    second[13] = byteSum(second.data(), 13);

    DurationBuilder b;
    b.setTimings(500, 325, 500, 1050);
    b.leader(3000, 1650);
    appendBytes(b, first);
    b.add(500);
    b.add(70000);
    b.leader(3000, 1650);
    appendBytes(b, second);
    b.add(500);
    return b.out;
}

std::vector<std::uint32_t> generateMi1Durations(const OfficialAcState& state) {
    static constexpr std::array<std::uint8_t, 12> tempMap = {0, 1, 3, 2, 1, 3, 2, 4, 13, 15, 14, 8};
    static constexpr std::array<std::uint8_t, 2> powerMap = {0, 3};
    static constexpr std::array<std::uint8_t, 2> sleepMap = {0, 3};
    static constexpr std::array<std::uint8_t, 3> fanMap = {0, 1, 15};
    static constexpr std::array<std::uint8_t, 8> powerModeMap = {1, 3, 2, 4, 13, 15, 14, 8};

    const auto power = f(state, 32);
    const auto mode = f(state, 33);
    auto temperature = f(state, 34);
    Bytes bytes(12, 0);
    bytes[0] = 0xE7;
    bytes[1] = 0x32;
    bytes[2] = 0x00;
    bytes[3] = 0x10;
    bytes[4] = 0xC5;
    bytes[5] = static_cast<std::uint8_t>(powerModeMap.at((power ? 4 : 0) + mode) | (16U * f(state, 37)));
    bytes[6] = fanMap.at(f(state, 35));
    if (temperature > 0x0F) {
        temperature = static_cast<std::uint8_t>(temperature - 16U);
        bytes[7] = static_cast<std::uint8_t>((tempMap.at(temperature & 3U) + 4U * tempMap.at(temperature >> 2U)) | 0x20U);
    } else {
        bytes[7] = static_cast<std::uint8_t>((tempMap.at(temperature & 3U) + 4U * tempMap.at(temperature >> 2U)) | 0x30U);
    }
    bytes[8] = static_cast<std::uint8_t>(4U * powerMap.at(f(state, 36)));

    const auto timerMode = static_cast<std::uint8_t>(f(state, 39) + 2U * f(state, 41));
    std::uint8_t timerBits = 0;
    if (timerMode == 0 || timerMode == 1) {
        timerBits = 0;
    } else {
        timerBits = static_cast<std::uint8_t>(4U * (timerMode != 2) + 4U);
    }
    if (mode <= 1) {
        bytes[9] = static_cast<std::uint8_t>(sleepMap.at(f(state, 40)) | timerBits | 0x40U);
        bytes[10] = mode == 1 ? 38 : 0;
    } else {
        bytes[9] = static_cast<std::uint8_t>(sleepMap.at(f(state, 40)) | timerBits);
        bytes[10] = 0;
    }
    bytes[11] = byteXor(bytes.data(), 11);

    std::vector<std::uint32_t> durations;
    durations.reserve(99);
    durations.push_back(1000);
    durations.push_back(500);
    for (const auto byte : bytes) {
        auto value = byte;
        for (int i = 0; i < 4; ++i) {
            durations.push_back(630);
            switch (value >> 6U) {
                case 0:
                    durations.push_back(330);
                    break;
                case 1:
                    durations.push_back(830);
                    break;
                case 2:
                    durations.push_back(1430);
                    break;
                default:
                    durations.push_back(2130);
                    break;
            }
            value = static_cast<std::uint8_t>(value << 2U);
        }
    }
    durations.push_back(630);
    return durations;
}

std::vector<std::uint32_t> generateChigo1Durations(const OfficialAcState& state) {
    static constexpr std::array<std::uint8_t, 4> fanMap = {0, 3, 2, 1};
    static constexpr std::array<std::uint8_t, 3> swingMap = {1, 3, 5};

    Bytes bytes(12, 0);
    bytes[3] = static_cast<std::uint8_t>(f(state, 40) | (8U * f(state, 39)));
    auto swing = swingMap.at(f(state, 37));
    if (f(state, 32) == 0) {
        swing = static_cast<std::uint8_t>(swing & 0xFEU);
    }
    bytes[7] = static_cast<std::uint8_t>((2U * swing) | (32U * fanMap.at(f(state, 35))) |
                                         f(state, 36) | (16U * f(state, 38)));
    bytes[9] = static_cast<std::uint8_t>(f(state, 34) | (32U * f(state, 33)));
    bytes[11] = 0xD5;
    for (std::size_t i = 0; i < bytes.size(); i += 2) {
        const auto source = (i + 1 < bytes.size()) ? bytes[i + 1] : 0;
        bytes[i] = bitNot(source);
    }

    DurationBuilder b;
    b.setTimings(510, 560, 510, 1680);
    b.leader(6100, 7400);
    appendBytes(b, bytes);
    b.add(510);
    b.add(7400);
    b.add(510);
    return b.out;
}

std::vector<std::uint32_t> generateHaier1Durations(const OfficialAcState& state) {
    static constexpr std::array<std::uint8_t, 5> modeMap = {0, 2, 4, 12, 8};
    static constexpr std::array<std::uint8_t, 4> fanMap = {5, 3, 2, 1};
    static constexpr std::array<std::uint8_t, 6> tempEvenMap = {0, 6, 8, 10, 12, 14};
    static constexpr std::array<std::uint8_t, 6> tempOddMap = {2, 4, 6, 8, 10, 12};

    const auto power = f(state, 32);
    const auto mode = f(state, 33);
    const auto timer = f(state, 42);
    std::uint8_t powerTimer = 0;
    bool timerOnPower = false;
    bool timerOnOff = false;
    std::uint8_t vane = 0;
    if (timer != 0) {
        powerTimer = power == 0 ? 64 : 32;
        timerOnPower = power != 0;
        timerOnOff = power == 0;
        vane = f(state, 36);
        if (vane != 0 || mode == 3) {
            vane = 0;
        }
    } else {
        vane = static_cast<std::uint8_t>(f(state, 36) << 7U);
    }

    const auto healthBit = ((mode & 0xFBU) == 0) ? static_cast<std::uint8_t>(f(state, 39) << 7U) : 0;
    const auto temp = f(state, 34);
    const auto fanBits = static_cast<std::uint8_t>(32U * fanMap.at(f(state, 35)));
    const auto modeBits = static_cast<std::uint8_t>(16U * modeMap.at(mode));
    const auto vaneByte = static_cast<std::uint8_t>(vane | (f(state, 43) << 6U));

    Bytes bytes(22, 0);
    bytes[0] = 0xA6;
    bytes[1] = static_cast<std::uint8_t>(tempOddMap.at(f(state, 37)) | (16U * (temp >> 1U)));
    bytes[2] = static_cast<std::uint8_t>(16U * tempEvenMap.at(f(state, 38)));
    bytes[3] = static_cast<std::uint8_t>((2U * f(state, 41)) | powerTimer);
    bytes[4] = static_cast<std::uint8_t>((power << 6U) | healthBit);
    bytes[5] = fanBits;
    bytes[7] = modeBits;
    bytes[8] = vaneByte;
    if (timerOnPower) {
        if (timer <= 0x17U) {
            bytes[5] = static_cast<std::uint8_t>(fanBits | timer);
        } else {
            bytes[6] = 0x3B;
            bytes[5] = static_cast<std::uint8_t>(fanBits | 0x17U);
        }
    }
    if (timerOnOff) {
        if (timer <= 0x17U) {
            bytes[7] = static_cast<std::uint8_t>(modeBits | timer);
        } else {
            bytes[7] = static_cast<std::uint8_t>(modeBits | 0x17U);
            bytes[8] = static_cast<std::uint8_t>(vaneByte | 0x3BU);
        }
    }
    bytes[10] = static_cast<std::uint8_t>(temp & 1U);
    bytes[11] = static_cast<std::uint8_t>((mode == 2 ? 23U : 0U) | (f(state, 40) << 6U));
    bytes[12] = 5;
    bytes[13] = byteSum(bytes.data(), 13);
    bytes[14] = 0xB7;
    bytes[21] = 0xB7;

    DurationBuilder b;
    b.setTimings(520, 580, 520, 1680);
    b.leader(3000, 3000);
    b.leader(3000, 4500);
    appendBytes(b, bytes, true);
    b.add(520);
    return b.out;
}

std::vector<std::uint32_t> generateHaier2Durations(const OfficialAcState& state) {
    static constexpr std::array<std::uint8_t, 5> modeMap = {0, 2, 4, 12, 8};
    static constexpr std::array<std::uint8_t, 4> fanMap = {5, 3, 2, 1};
    static constexpr std::array<std::uint8_t, 2> swingMap = {0, 3};
    static constexpr std::array<std::uint8_t, 2> timerLowMap = {0, 30};

    const auto power = f(state, 32);
    const auto mode = f(state, 33);
    const auto timer = f(state, 40);
    bool timerOnPower = false;
    bool timerOnOff = false;
    std::uint8_t timerPower = 0;
    std::uint8_t timerHalf = 0;
    std::uint8_t timerOdd = 0;
    std::uint8_t vane = f(state, 36);
    if (timer != 0) {
        timerOnPower = power != 0;
        timerOnOff = power == 0;
        timerPower = power == 0 ? 64 : 32;
        if (timer > 0x17U) {
            timerHalf = static_cast<std::uint8_t>(timer - 12U);
        } else {
            timerOdd = static_cast<std::uint8_t>(timer & 1U);
            timerHalf = static_cast<std::uint8_t>(timer >> 1U);
        }
        if (vane != 0) {
            vane = 0;
        }
    }

    const auto healthBit = ((mode & 0xFBU) == 0) ? static_cast<std::uint8_t>(f(state, 42) << 7U) : 0;
    std::uint8_t modeExtra = 0;
    if (mode == 1 || mode == 4) {
        modeExtra = static_cast<std::uint8_t>((f(state, 39) << 6U) | (f(state, 38) << 7U));
    }
    const auto fanBits = static_cast<std::uint8_t>(32U * fanMap.at(f(state, 34)));
    const auto modeBits = static_cast<std::uint8_t>(16U * modeMap.at(mode));

    Bytes bytes(14, 0);
    bytes[0] = 0xA6;
    bytes[1] = static_cast<std::uint8_t>((4U * swingMap.at(f(state, 37))) | (16U * f(state, 35)));
    bytes[3] = static_cast<std::uint8_t>((2U * f(state, 41)) | timerPower);
    bytes[4] = static_cast<std::uint8_t>((power << 6U) | healthBit);
    bytes[5] = fanBits;
    bytes[6] = modeExtra;
    bytes[7] = modeBits;
    bytes[8] = 0;
    bytes[10] = static_cast<std::uint8_t>(16U * f(state, 43));

    if (timerOnPower) {
        if (timerHalf <= 0x17U) {
            bytes[5] = static_cast<std::uint8_t>(fanBits | timerHalf);
            bytes[6] = static_cast<std::uint8_t>(modeExtra | timerLowMap.at(timerOdd));
        } else {
            bytes[5] = static_cast<std::uint8_t>(fanBits | 0x17U);
            bytes[6] = static_cast<std::uint8_t>(modeExtra | 0x3BU);
        }
    }
    if (timerOnOff) {
        if (timerHalf <= 0x17U) {
            bytes[8] = timerLowMap.at(timerOdd);
            bytes[7] = static_cast<std::uint8_t>(modeBits | timerHalf);
        } else {
            bytes[8] = 0x3B;
            bytes[7] = static_cast<std::uint8_t>(modeBits | 0x17U);
        }
    }
    if (vane != 0) {
        bytes[5] = static_cast<std::uint8_t>(bytes[5] | 0x08U);
        bytes[8] = static_cast<std::uint8_t>(bytes[8] | 0x80U);
    }
    bytes[12] = 5;
    bytes[13] = byteSum(bytes.data(), 13);

    DurationBuilder b;
    b.setTimings(520, 580, 520, 1680);
    b.leader(3000, 3000);
    b.leader(3000, 4500);
    appendBytes(b, bytes, true);
    b.add(520);
    return b.out;
}

std::vector<std::uint32_t> generateHaier3Durations(const OfficialAcState& state) {
    static constexpr std::array<std::uint8_t, 2> powerMap = {1, 0};
    static constexpr std::array<std::uint8_t, 4> fanMap = {0, 3, 2, 1};
    static constexpr std::array<std::uint8_t, 5> modeMap = {0, 2, 4, 8, 6};

    Bytes bytes(9, 0);
    bytes[0] = 0xA5;
    bytes[1] = static_cast<std::uint8_t>(f(state, 33) | (16U * f(state, 35)));
    bytes[2] = static_cast<std::uint8_t>((f(state, 38) & 0x1FU) | (32U * powerMap.at(f(state, 37))));
    bytes[3] = f(state, 39);
    bytes[4] = 0x0C;
    bytes[5] = static_cast<std::uint8_t>(fanMap.at(f(state, 34)) << 6U);
    bytes[6] = static_cast<std::uint8_t>(16U * modeMap.at(f(state, 32)));
    bytes[7] = static_cast<std::uint8_t>(f(state, 36) << 6U);
    bytes[8] = byteSum(bytes.data(), 8);

    DurationBuilder b;
    b.setTimings(560, 560, 560, 1680);
    b.leader(3000, 3000);
    b.leader(3000, 4500);
    appendBytes(b, bytes, true);
    b.add(560);
    return b.out;
}

std::vector<std::uint32_t> generateHaier4Durations(const OfficialAcState& state) {
    static constexpr std::array<std::uint8_t, 5> modeMap = {0, 2, 4, 12, 8};
    static constexpr std::array<std::uint8_t, 4> fanMap = {5, 3, 2, 1};
    static constexpr std::array<std::uint8_t, 6> tempEvenMap = {0, 6, 8, 10, 12, 14};
    static constexpr std::array<std::uint8_t, 6> tempOddMap = {2, 4, 6, 8, 10, 12};

    const auto power = f(state, 33);
    const auto mode = f(state, 32);
    const auto timer = f(state, 43);
    bool timerOnPower = false;
    bool timerOnOff = false;
    std::uint8_t timerPower = 0;
    std::uint8_t vane = 0;
    if (timer != 0) {
        timerPower = power == 0 ? 64 : 32;
        timerOnPower = power != 0;
        timerOnOff = power == 0;
        vane = f(state, 36);
        if (vane != 0 || mode == 3) {
            vane = 0;
        }
    } else {
        vane = static_cast<std::uint8_t>(f(state, 36) << 7U);
    }

    std::uint8_t healthBit = 0;
    std::uint8_t modeExtra = 0;
    if ((mode & 0xFBU) == 0) {
        healthBit = static_cast<std::uint8_t>(f(state, 42) << 7U);
        modeExtra = static_cast<std::uint8_t>((f(state, 40) << 7U) | (f(state, 39) << 6U));
    }
    const auto temp = f(state, 35);
    const auto fanBits = static_cast<std::uint8_t>(32U * fanMap.at(f(state, 34)));
    const auto modeBits = static_cast<std::uint8_t>(16U * modeMap.at(mode));

    Bytes bytes(20, 0);
    bytes[0] = 0xA6;
    bytes[1] = static_cast<std::uint8_t>(tempOddMap.at(f(state, 37)) | (16U * (temp >> 1U)));
    bytes[2] = static_cast<std::uint8_t>(16U * tempEvenMap.at(f(state, 38)));
    bytes[3] = static_cast<std::uint8_t>((2U * f(state, 41)) | timerPower);
    bytes[4] = static_cast<std::uint8_t>(healthBit | (power << 6U));
    bytes[5] = fanBits;
    bytes[6] = modeExtra;
    bytes[7] = modeBits;
    bytes[8] = vane;
    if (timerOnPower) {
        if (timer > 0x17U) {
            bytes[5] = static_cast<std::uint8_t>(fanBits | 0x17U);
            bytes[6] = static_cast<std::uint8_t>(modeExtra | 0x3BU);
        } else {
            bytes[5] = static_cast<std::uint8_t>(fanBits | timer);
        }
    }
    if (timerOnOff) {
        if (timer > 0x17U) {
            bytes[7] = static_cast<std::uint8_t>(modeBits | 0x17U);
            bytes[8] = static_cast<std::uint8_t>(vane | 0x3BU);
        } else {
            bytes[7] = static_cast<std::uint8_t>(modeBits | timer);
        }
    }
    bytes[10] = static_cast<std::uint8_t>(temp & 1U);
    bytes[12] = 5;
    bytes[13] = byteSum(bytes.data(), 13);
    bytes[14] = 0xB5;
    bytes[19] = 0xB5;

    DurationBuilder b;
    b.setTimings(560, 560, 560, 1680);
    b.leader(3000, 3000);
    b.leader(3000, 4500);
    appendBytes(b, bytes, true);
    b.add(560);
    return b.out;
}

std::vector<std::uint32_t> generateHisense3Durations(const OfficialAcState& state) {
    static constexpr std::array<std::uint8_t, 5> modeMap = {1, 2, 3, 4, 0};
    static constexpr std::array<std::uint8_t, 4> fanMap = {0, 3, 2, 1};

    std::uint8_t fanBits = 0;
    std::uint8_t fanValue = fanMap.at(f(state, 35));
    if (f(state, 32) != 0 && f(state, 36) != 0) {
        fanBits = static_cast<std::uint8_t>(8U * f(state, 36));
        fanValue = 3;
    }

    Bytes first(6, 0);
    Bytes second(8, 0);
    first[0] = 0x83;
    first[1] = 0x06;
    first[2] = static_cast<std::uint8_t>((4U * f(state, 41)) | fanBits | fanValue);
    first[3] = static_cast<std::uint8_t>((f(state, 39) != 0 ? 8U : 0U) |
                                         modeMap.at(f(state, 33)) |
                                         (16U * f(state, 34)));
    first[4] = f(state, 39);
    first[5] = static_cast<std::uint8_t>(32U * f(state, 40));
    second[2] = static_cast<std::uint8_t>((f(state, 38) << 7U) | (f(state, 37) << 6U));

    Bytes checksumBytes;
    checksumBytes.reserve(11);
    checksumBytes.insert(checksumBytes.end(), first.begin() + 2, first.end());
    checksumBytes.insert(checksumBytes.end(), second.begin(), second.begin() + 7);
    second[7] = byteXor(checksumBytes.data(), checksumBytes.size());

    DurationBuilder b;
    b.setTimings(560, 560, 560, 1680);
    b.leader(9000, 4500);
    appendBytes(b, first);
    b.add(560);
    b.add(8000);
    appendBytes(b, second);
    b.add(560);
    return b.out;
}

std::vector<std::uint32_t> generateHisense1Durations(const OfficialAcState& state) {
    static constexpr std::array<std::uint8_t, 5> modeMap = {17, 2, 3, 4, 16};
    static constexpr std::array<std::uint8_t, 6> fanMap = {0xFF, 0x81, 0x8A, 0xAD, 0xD0, 0xE4};
    static constexpr std::array<std::uint8_t, 2> tempMap = {5, 3};

    const auto timer = f(state, 36);
    std::uint8_t timerHalf = 0;
    std::uint8_t timerOdd = 0;
    bool timerOnPower = false;
    bool timerOnOff = false;
    if (timer != 0 && f(state, 40) == 0) {
        if (timer <= 0x13U) {
            timerHalf = static_cast<std::uint8_t>(timer >> 1U);
            timerOdd = static_cast<std::uint8_t>(timer & 1U);
        } else {
            timerHalf = static_cast<std::uint8_t>(timer - 10U);
        }
        timerOnPower = f(state, 32) != 0;
        timerOnOff = f(state, 32) == 0;
    }

    Bytes bytes(23, 0);
    bytes[0] = 0x60;
    bytes[1] = 0x38;
    bytes[2] = 0x13;
    bytes[3] = static_cast<std::uint8_t>(f(state, 32) | (2U * modeMap.at(f(state, 33))));
    bytes[4] = fanMap.at(f(state, 35));
    bytes[6] = static_cast<std::uint8_t>(5U * f(state, 34));
    bytes[7] = 0x40;
    bytes[8] = 0xE6;
    bytes[11] = static_cast<std::uint8_t>(f(state, 39) << 7U);
    if (timerOnPower) {
        bytes[12] = static_cast<std::uint8_t>(timerHalf | (f(state, 40) << 6U) | 0x20U);
        bytes[13] = static_cast<std::uint8_t>((-timerOdd) & 0x1EU);
    }
    if (timerOnOff) {
        bytes[14] = static_cast<std::uint8_t>(timerHalf | 0x20U);
        bytes[15] = timerOdd == 0 ? 0x80 : 0x9E;
    } else {
        bytes[15] = 0x80;
    }
    bytes[16] = static_cast<std::uint8_t>(f(state, 37) | 0x90U);
    bytes[17] = static_cast<std::uint8_t>(tempMap.at(f(state, 38)) | 0x60U);
    bytes[18] = (f(state, 32) != 0 && f(state, 33) == 2) ? 22 : 0;
    bytes[19] = 37;
    bytes[22] = byteXor(bytes.data() + 3, 19);

    DurationBuilder b;
    b.setTimings(560, 560, 560, 1680);
    b.leader(9000, 4500);
    appendBytes(b, bytes);
    b.add(560);
    return b.out;
}

std::vector<std::uint32_t> generateHisense2Durations(const OfficialAcState& state) {
    static constexpr std::array<std::uint8_t, 5> modeMap = {1, 2, 3, 4, 0};
    static constexpr std::array<std::uint8_t, 5> fanMap = {0, 0, 3, 2, 1};
    static constexpr std::array<std::uint8_t, 5> tempSpecialMap = {2, 1, 0, 5, 6};

    const auto turbo = f(state, 36);
    const auto savedFan = f(state, 35);
    const auto timer = f(state, 38);
    std::uint8_t fanBits = fanMap.at(savedFan);
    if (turbo != 0) {
        fanBits = 3;
    }
    std::uint8_t timerEncoded = timer <= 9 ? static_cast<std::uint8_t>(2U * timer)
                                           : static_cast<std::uint8_t>(timer + 10U);
    bool timerOnPower = false;
    bool timerOnOff = false;
    if (timer != 0) {
        timerOnPower = f(state, 32) != 0;
        timerOnOff = f(state, 32) == 0;
    }

    Bytes memory(21, 0);
    memory[0] = 0x83;
    memory[1] = 0x06;
    memory[2] = static_cast<std::uint8_t>((8U * turbo) | (4U * f(state, 39)) | fanBits);
    auto temperature = f(state, 34);
    std::uint8_t tempNibble = static_cast<std::uint8_t>(16U * temperature);
    if ((f(state, 33) & 0xFDU) == 0) {
        if (temperature >= 5 && temperature <= 9) {
            tempNibble = 0x70;
            memory[2] = static_cast<std::uint8_t>((16U * tempSpecialMap.at(9 - temperature)) | memory[2]);
        } else {
            if (temperature > 4) {
                memory[2] = static_cast<std::uint8_t>(memory[2] | 0x20U);
                memory[16] = static_cast<std::uint8_t>(4U * temperature - 36U);
            } else {
                memory[2] = static_cast<std::uint8_t>(memory[2] | 0x60U);
                memory[16] = static_cast<std::uint8_t>(4U * (5U - temperature));
            }
            tempNibble = 0x70;
        }
    }
    memory[3] = static_cast<std::uint8_t>(modeMap.at(f(state, 33)) | tempNibble);
    memory[4] = static_cast<std::uint8_t>(timerEncoded | (f(state, 37) << 6U));
    if (timerOnPower) {
        memory[7] = 0x80;
        memory[8] = timer;
        memory[9] = 1;
    }
    if (timerOnOff) {
        memory[9] = 0x80;
        memory[11] = 1;
        memory[10] = static_cast<std::uint8_t>(timer | 0x20U);
    } else {
        memory[10] = 32;
    }
    memory[12] = f(state, 33) == 4 ? 18 : 32;
    memory[13] = byteXor(memory.data() + 2, 11);
    memory[14] = static_cast<std::uint8_t>(4U * (savedFan == 1));
    memory[15] = 2;
    memory[18] = static_cast<std::uint8_t>((16U * f(state, 32)) | 0x20U);
    memory[19] = 21;
    memory[20] = byteXor(memory.data() + 2, 18);

    DurationBuilder b;
    b.setTimings(560, 560, 560, 1680);
    b.leader(9000, 4500);
    appendBytes(b, Bytes(memory.begin(), memory.begin() + 6));
    b.add(560);
    b.add(8000);
    appendBytes(b, Bytes(memory.begin() + 6, memory.end()));
    b.add(560);
    return b.out;
}

std::vector<std::uint32_t> generateDaikin2Durations(const OfficialAcState& state) {
    static constexpr std::array<std::uint8_t, 5> modeMap = {0, 3, 2, 6, 4};
    static constexpr std::array<std::uint8_t, 7> fanMap = {10, 11, 3, 4, 5, 6, 7};
    static constexpr std::array<std::uint8_t, 2> swingMap = {0, 15};

    const auto power = f(state, 32);
    const auto timer = f(state, 38);
    std::uint8_t timerModeBits = 0;
    std::uint8_t timerPowerBits = 0;
    bool timerOnPower = false;
    bool timerOnOff = false;
    std::uint16_t timerEncoded = 0;
    if (timer != 0) {
        timerModeBits = power == 0 ? 2 : 0;
        timerOnPower = power != 0;
        timerOnOff = power == 0;
        timerEncoded = static_cast<std::uint16_t>(60U * timer);
        timerPowerBits = power != 0 ? 4 : 0;
    }

    auto fan = f(state, 35);
    if (f(state, 39) != 0) {
        fan = 2;
    }
    const auto mode = f(state, 33);

    Bytes bytes(19, 0);
    bytes[0] = 0x11;
    bytes[1] = 0xDA;
    bytes[2] = 0x27;
    bytes[5] = static_cast<std::uint8_t>(timerModeBits | (16U * modeMap.at(mode)) | power | timerPowerBits);
    if (mode == 2) {
        bytes[6] = 0xC0;
        fan = 0;
        bytes[8] = 0xA0;
    } else {
        bytes[6] = static_cast<std::uint8_t>(2U * f(state, 34) + 36U);
        bytes[8] = static_cast<std::uint8_t>(16U * fanMap.at(fan));
    }
    bytes[8] = static_cast<std::uint8_t>(bytes[8] | swingMap.at(f(state, 37)));
    if (timerOnOff) {
        bytes[10] = static_cast<std::uint8_t>(timerEncoded & 0xFFU);
        bytes[11] = static_cast<std::uint8_t>((timerEncoded >> 8U) & 0x0FU);
    }
    if (timerOnPower) {
        const auto value = static_cast<std::uint16_t>(16U * timerEncoded);
        bytes[11] = static_cast<std::uint8_t>(value & 0xFFU);
        bytes[12] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    }
    bytes[13] = static_cast<std::uint8_t>(f(state, 36) | (32U * f(state, 41)));
    bytes[15] = 0xC5;
    bytes[16] = power == 0 ? 0x40 : 0x00;
    bytes[18] = byteSum(bytes.data(), 18);

    DurationBuilder b;
    b.setTimings(430, 420, 430, 1320);
    for (int i = 0; i < 11; ++i) {
        b.add(430);
    }
    b.add(25000);
    b.leader(3500, 1750);
    appendBytes(b, bytes);
    b.add(430);
    return b.out;
}

}  // namespace

std::uint8_t OfficialAcState::field(int officialOffset) const {
    if (officialOffset < 32 || officialOffset >= 64) {
        throw std::out_of_range("official AC field offset must be 32..63");
    }
    return fields[static_cast<std::size_t>(officialOffset - 32)];
}

void OfficialAcState::setField(int officialOffset, std::uint8_t value) {
    if (officialOffset < 32 || officialOffset >= 64) {
        throw std::out_of_range("official AC field offset must be 32..63");
    }
    fields[static_cast<std::size_t>(officialOffset - 32)] = value;
}

const std::vector<OfficialAcProtocol>& officialAcProtocols() {
    static const std::vector<OfficialAcProtocol> protocols = {
        OfficialAcProtocol::Gree1,
        OfficialAcProtocol::Midea1,
        OfficialAcProtocol::Gree2,
        OfficialAcProtocol::Tcl1,
        OfficialAcProtocol::Aux1,
        OfficialAcProtocol::Haier1,
        OfficialAcProtocol::Mi1,
        OfficialAcProtocol::Chigo1,
        OfficialAcProtocol::Daikin1,
        OfficialAcProtocol::Chiq1,
        OfficialAcProtocol::Panasonic,
        OfficialAcProtocol::Midea2,
        OfficialAcProtocol::Gree3,
        OfficialAcProtocol::Mitsubishi1,
        OfficialAcProtocol::Hisense2,
        OfficialAcProtocol::Hisense3,
        OfficialAcProtocol::Hisense1,
        OfficialAcProtocol::Haier2,
        OfficialAcProtocol::Haier3,
        OfficialAcProtocol::Haier4,
        OfficialAcProtocol::Daikin2,
    };
    return protocols;
}

const char* officialAcProtocolName(OfficialAcProtocol protocol) {
    switch (protocol) {
        case OfficialAcProtocol::Gree1:
            return "gree1";
        case OfficialAcProtocol::Midea1:
            return "midea1";
        case OfficialAcProtocol::Gree2:
            return "gree2";
        case OfficialAcProtocol::Tcl1:
            return "tcl1";
        case OfficialAcProtocol::Aux1:
            return "aux1";
        case OfficialAcProtocol::Haier1:
            return "haier1";
        case OfficialAcProtocol::Mi1:
            return "mi1";
        case OfficialAcProtocol::Chigo1:
            return "chigo1";
        case OfficialAcProtocol::Daikin1:
            return "daikin1";
        case OfficialAcProtocol::Chiq1:
            return "chiq1";
        case OfficialAcProtocol::Panasonic:
            return "panasonic";
        case OfficialAcProtocol::Midea2:
            return "midea2";
        case OfficialAcProtocol::Gree3:
            return "gree3";
        case OfficialAcProtocol::Mitsubishi1:
            return "mitsubishi1";
        case OfficialAcProtocol::Hisense2:
            return "hisense2";
        case OfficialAcProtocol::Hisense3:
            return "hisense3";
        case OfficialAcProtocol::Hisense1:
            return "hisense1";
        case OfficialAcProtocol::Haier2:
            return "haier2";
        case OfficialAcProtocol::Haier3:
            return "haier3";
        case OfficialAcProtocol::Haier4:
            return "haier4";
        case OfficialAcProtocol::Daikin2:
            return "daikin2";
    }
    throw std::invalid_argument("unknown official AC protocol");
}

std::optional<OfficialAcProtocol> parseOfficialAcProtocol(const std::string& text) {
    const auto key = normalizedProtocol(text);
    for (const auto protocol : officialAcProtocols()) {
        if (key == normalizedProtocol(officialAcProtocolName(protocol))) {
            return protocol;
        }
    }
    if (key == "gree" || key == "geli") {
        return OfficialAcProtocol::Gree1;
    }
    if (key == "midea" || key == "meidi") {
        return OfficialAcProtocol::Midea1;
    }
    if (key == "aux" || key == "aokesi") {
        return OfficialAcProtocol::Aux1;
    }
    if (key == "mi" || key == "xiaomi") {
        return OfficialAcProtocol::Mi1;
    }
    if (key == "mitsubishi" || key == "sanling") {
        return OfficialAcProtocol::Mitsubishi1;
    }
    return std::nullopt;
}

OfficialAcState defaultOfficialAcState(OfficialAcProtocol protocol) {
    OfficialAcState state;
    state.protocol = protocol;
    auto set = [&](int offset, std::uint8_t value) {
        state.setField(offset, value);
    };

    switch (protocol) {
        case OfficialAcProtocol::Gree1:
        case OfficialAcProtocol::Gree2:
            set(33, 1);
            set(32, 1);
            set(37, 8);
            set(40, 1);
            break;
        case OfficialAcProtocol::Midea1:
            set(32, 1);
            set(33, 1);
            set(34, 7);
            break;
        case OfficialAcProtocol::Midea2:
            set(32, 1);
            set(33, 1);
            set(34, 7);
            break;
        case OfficialAcProtocol::Gree3:
            set(33, 1);
            set(32, 1);
            set(36, 8);
            set(38, 1);
            break;
        case OfficialAcProtocol::Tcl1:
            set(32, 1);
            set(33, 1);
            set(34, 8);
            set(36, 1);
            break;
        case OfficialAcProtocol::Aux1:
            set(32, 1);
            set(33, 1);
            set(34, 8);
            break;
        case OfficialAcProtocol::Haier1:
            set(32, 1);
            set(33, 1);
            set(34, 16);
            break;
        case OfficialAcProtocol::Mi1:
            set(32, 1);
            set(34, 16);
            set(41, 1);
            break;
        case OfficialAcProtocol::Chigo1:
            set(32, 1);
            set(34, 16);
            break;
        case OfficialAcProtocol::Daikin1:
            set(32, 1);
            set(33, 1);
            set(34, 8);
            break;
        case OfficialAcProtocol::Chiq1:
            set(32, 1);
            set(34, 8);
            break;
        case OfficialAcProtocol::Panasonic:
            set(33, 1);
            set(32, 1);
            set(35, 8);
            break;
        case OfficialAcProtocol::Mitsubishi1:
            set(32, 1);
            set(33, 1);
            set(34, 8);
            set(38, 2);
            break;
        case OfficialAcProtocol::Hisense2:
            set(32, 1);
            set(33, 1);
            set(34, 6);
            break;
        case OfficialAcProtocol::Hisense3:
            set(32, 1);
            set(33, 1);
            set(34, 6);
            break;
        case OfficialAcProtocol::Hisense1:
            set(32, 1);
            set(33, 1);
            set(34, 6);
            break;
        case OfficialAcProtocol::Haier2:
            set(32, 1);
            set(33, 1);
            set(35, 8);
            break;
        case OfficialAcProtocol::Haier3:
            set(33, 1);
            set(32, 1);
            set(35, 8);
            break;
        case OfficialAcProtocol::Haier4:
            set(33, 1);
            set(32, 1);
            set(35, 8);
            break;
        case OfficialAcProtocol::Daikin2:
            set(32, 1);
            set(33, 1);
            set(34, 6);
            break;
        default:
            break;
    }
    return state;
}

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
            kMidea1FanTail.at(fan),
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

std::vector<std::uint32_t> generateOfficialAcDurations(const OfficialAcState& state) {
    switch (state.protocol) {
        case OfficialAcProtocol::Gree1:
            return generateGree1Durations(state);
        case OfficialAcProtocol::Gree2:
            return generateGree2Durations(state);
        case OfficialAcProtocol::Tcl1:
            return generateTcl1Durations(state);
        case OfficialAcProtocol::Aux1:
            return generateAux1Durations(state);
        case OfficialAcProtocol::Haier1:
            return generateHaier1Durations(state);
        case OfficialAcProtocol::Mi1:
            return generateMi1Durations(state);
        case OfficialAcProtocol::Chigo1:
            return generateChigo1Durations(state);
        case OfficialAcProtocol::Daikin1:
            return generateDaikin1Durations(state);
        case OfficialAcProtocol::Chiq1:
            return generateChiq1Durations(state);
        case OfficialAcProtocol::Panasonic:
            return generatePanasonicDurations(state);
        case OfficialAcProtocol::Gree3:
            return generateGree3Durations(state);
        case OfficialAcProtocol::Mitsubishi1:
            return generateMitsubishi1Durations(state);
        case OfficialAcProtocol::Hisense2:
            return generateHisense2Durations(state);
        case OfficialAcProtocol::Hisense3:
            return generateHisense3Durations(state);
        case OfficialAcProtocol::Hisense1:
            return generateHisense1Durations(state);
        case OfficialAcProtocol::Haier2:
            return generateHaier2Durations(state);
        case OfficialAcProtocol::Haier3:
            return generateHaier3Durations(state);
        case OfficialAcProtocol::Haier4:
            return generateHaier4Durations(state);
        case OfficialAcProtocol::Daikin2:
            return generateDaikin2Durations(state);
        case OfficialAcProtocol::Midea1: {
            OfficialMidea1Options options;
            options.power = f(state, 32) != 0;
            options.mode = static_cast<AcMode>(f(state, 33));
            options.temperature = 17 + f(state, 34);
            options.fan = static_cast<AcFan>(f(state, 35));
            options.swingIndex = f(state, 36);
            options.timerIndex = f(state, 37);
            return generateMidea1Durations(options);
        }
        case OfficialAcProtocol::Midea2: {
            OfficialMidea2Options options;
            options.power = f(state, 32) != 0;
            options.mode = static_cast<AcMode>(f(state, 33));
            options.temperature = 17 + f(state, 34);
            options.fan = static_cast<AcFan>(f(state, 35));
            options.swingIndex = f(state, 36);
            return generateMidea2Durations(options);
        }
    }
    throw std::invalid_argument("unknown official AC protocol");
}

Bytes encodeOfficialAc(const OfficialAcState& state) {
    return encodeDurationsAsExternalCode(generateOfficialAcDurations(state));
}

}  // namespace echoir
