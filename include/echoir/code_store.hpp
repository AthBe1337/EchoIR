#pragma once

#include "echoir/frame.hpp"

#include <cstdint>
#include <string>

namespace echoir {

struct StoredCode {
    std::string name;
    std::string createdAt;
    std::string note;
    std::uint8_t address = 0xFF;
    std::uint8_t afn = 0x22;
    Bytes data;
    Bytes frame;
};

std::string currentTimestamp();
void saveStoredCode(const std::string& path, const StoredCode& code);
StoredCode loadStoredCode(const std::string& path);

}  // namespace echoir

