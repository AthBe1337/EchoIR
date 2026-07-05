#include "echoir/frame.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>

namespace echoir {
namespace {

constexpr std::uint8_t kFrameHead = 0x68;
constexpr std::uint8_t kFrameTail = 0x16;
constexpr std::size_t kMinFrameLength = 7;

bool checksumMatches(std::uint8_t checksum,
                     std::uint8_t address,
                     std::uint8_t afn,
                     const Bytes& data,
                     ChecksumPolicy policy) {
    const auto sum = standardChecksum(address, afn, data);
    if (policy == ChecksumPolicy::StandardOnly) {
        return checksum == sum;
    }
    if (policy == ChecksumPolicy::Fixed55Only) {
        return checksum == 0x55;
    }
    return checksum == sum || checksum == 0x55;
}

std::uint8_t hexValue(char c) {
    if (c >= '0' && c <= '9') {
        return static_cast<std::uint8_t>(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return static_cast<std::uint8_t>(10 + c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return static_cast<std::uint8_t>(10 + c - 'A');
    }
    throw ProtocolError("invalid hex character");
}

}  // namespace

std::uint8_t standardChecksum(std::uint8_t address, std::uint8_t afn, const Bytes& data) {
    unsigned int sum = address + afn;
    for (const auto byte : data) {
        sum += byte;
    }
    return static_cast<std::uint8_t>(sum & 0xFFU);
}

Bytes encodeFrame(std::uint8_t address,
                  std::uint8_t afn,
                  const Bytes& data,
                  ChecksumMode checksumMode) {
    const std::size_t length = kMinFrameLength + data.size();
    if (length > 0xFFFF) {
        throw ProtocolError("frame is too large");
    }

    Bytes frame;
    frame.reserve(length);
    frame.push_back(kFrameHead);
    frame.push_back(static_cast<std::uint8_t>(length & 0xFFU));
    frame.push_back(static_cast<std::uint8_t>((length >> 8U) & 0xFFU));
    frame.push_back(address);
    frame.push_back(afn);
    frame.insert(frame.end(), data.begin(), data.end());
    frame.push_back(checksumMode == ChecksumMode::Fixed55 ? 0x55 : standardChecksum(address, afn, data));
    frame.push_back(kFrameTail);
    return frame;
}

Frame decodeFrame(const Bytes& frameBytes, ChecksumPolicy policy) {
    if (frameBytes.size() < kMinFrameLength) {
        throw ProtocolError("frame is shorter than minimum length");
    }
    if (frameBytes.front() != kFrameHead) {
        throw ProtocolError("invalid frame head");
    }

    const std::size_t length = static_cast<std::size_t>(frameBytes[1]) |
                               (static_cast<std::size_t>(frameBytes[2]) << 8U);
    if (length != frameBytes.size()) {
        throw ProtocolError("frame length does not match payload size");
    }
    if (frameBytes.back() != kFrameTail) {
        throw ProtocolError("invalid frame tail");
    }

    Frame frame;
    frame.address = frameBytes[3];
    frame.afn = frameBytes[4];
    frame.data.assign(frameBytes.begin() + 5, frameBytes.end() - 2);
    frame.checksum = frameBytes[frameBytes.size() - 2];
    if (!checksumMatches(frame.checksum, frame.address, frame.afn, frame.data, policy)) {
        throw ProtocolError("invalid frame checksum");
    }
    return frame;
}

bool tryPopFrame(Bytes& buffer, Frame& frame, ChecksumPolicy policy) {
    while (!buffer.empty() && buffer.front() != kFrameHead) {
        buffer.erase(buffer.begin());
    }
    if (buffer.size() < 3) {
        return false;
    }

    const std::size_t length = static_cast<std::size_t>(buffer[1]) |
                               (static_cast<std::size_t>(buffer[2]) << 8U);
    if (length < kMinFrameLength) {
        buffer.erase(buffer.begin());
        return tryPopFrame(buffer, frame, policy);
    }
    if (buffer.size() < length) {
        return false;
    }

    Bytes candidate(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(length));
    try {
        frame = decodeFrame(candidate, policy);
        buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(length));
        return true;
    } catch (const ProtocolError&) {
        buffer.erase(buffer.begin());
        return tryPopFrame(buffer, frame, policy);
    }
}

std::string toHex(const Bytes& bytes, bool spaced) {
    std::ostringstream out;
    out << std::uppercase << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (spaced && i != 0) {
            out << ' ';
        }
        out << std::setw(2) << static_cast<unsigned int>(bytes[i]);
    }
    return out.str();
}

Bytes fromHex(const std::string& text) {
    std::string compact;
    compact.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '0' && i + 1 < text.size() && (text[i + 1] == 'x' || text[i + 1] == 'X')) {
            ++i;
            continue;
        }
        if (std::isxdigit(static_cast<unsigned char>(c))) {
            compact.push_back(c);
        } else if (std::isspace(static_cast<unsigned char>(c)) || c == ':' || c == '-' || c == ',') {
            continue;
        } else {
            throw ProtocolError("invalid character in hex text");
        }
    }
    if (compact.size() % 2 != 0) {
        throw ProtocolError("hex text must have an even number of digits");
    }

    Bytes bytes;
    bytes.reserve(compact.size() / 2);
    for (std::size_t i = 0; i < compact.size(); i += 2) {
        bytes.push_back(static_cast<std::uint8_t>((hexValue(compact[i]) << 4U) | hexValue(compact[i + 1])));
    }
    return bytes;
}

}  // namespace echoir
