#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace echoir {

using Bytes = std::vector<std::uint8_t>;

class ProtocolError : public std::runtime_error {
public:
    explicit ProtocolError(const std::string& message) : std::runtime_error(message) {}
};

enum class ChecksumMode {
    StandardSum,
    Fixed55,
};

enum class ChecksumPolicy {
    StandardOnly,
    Fixed55Only,
    StandardOrFixed55,
};

struct Frame {
    std::uint8_t address = 0;
    std::uint8_t afn = 0;
    Bytes data;
    std::uint8_t checksum = 0;
};

std::uint8_t standardChecksum(std::uint8_t address, std::uint8_t afn, const Bytes& data);
Bytes encodeFrame(std::uint8_t address,
                  std::uint8_t afn,
                  const Bytes& data,
                  ChecksumMode checksumMode = ChecksumMode::StandardSum);
Frame decodeFrame(const Bytes& frameBytes, ChecksumPolicy policy = ChecksumPolicy::StandardOnly);
bool tryPopFrame(Bytes& buffer, Frame& frame, ChecksumPolicy policy = ChecksumPolicy::StandardOnly);

std::string toHex(const Bytes& bytes, bool spaced = true);
Bytes fromHex(const std::string& text);

}  // namespace echoir

