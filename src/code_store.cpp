#include "echoir/code_store.hpp"

#include <ctime>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace echoir {
namespace {

std::string escapeJson(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char c : value) {
        switch (c) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped.push_back(c);
                break;
        }
    }
    return escaped;
}

std::string unescapeJson(std::string value) {
    std::string out;
    out.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\\' && i + 1 < value.size()) {
            ++i;
            switch (value[i]) {
                case 'n':
                    out.push_back('\n');
                    break;
                case 'r':
                    out.push_back('\r');
                    break;
                case 't':
                    out.push_back('\t');
                    break;
                default:
                    out.push_back(value[i]);
                    break;
            }
        } else {
            out.push_back(value[i]);
        }
    }
    return out;
}

std::string getStringField(const std::string& text, const std::string& key, bool required) {
    const std::regex field("\"" + key + "\"\\s*:\\s*\"((\\\\.|[^\\\\\"])*)\"");
    std::smatch match;
    if (!std::regex_search(text, match, field)) {
        if (required) {
            throw std::runtime_error("missing JSON field: " + key);
        }
        return {};
    }
    return unescapeJson(match[1].str());
}

std::uint8_t parseByteField(const std::string& value, const std::string& field) {
    const Bytes bytes = fromHex(value);
    if (bytes.size() != 1) {
        throw std::runtime_error(field + " must contain exactly one byte");
    }
    return bytes[0];
}

}  // namespace

std::string currentTimestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm tm {};
#if defined(_WIN32)
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S%z");
    return out.str();
}

void saveStoredCode(const std::string& path, const StoredCode& code) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("failed to open output file: " + path);
    }
    out << "{\n";
    out << "  \"format\": \"echoir-code-v1\",\n";
    out << "  \"name\": \"" << escapeJson(code.name) << "\",\n";
    out << "  \"created_at\": \"" << escapeJson(code.createdAt) << "\",\n";
    out << "  \"note\": \"" << escapeJson(code.note) << "\",\n";
    out << "  \"address\": \"" << toHex({code.address}, false) << "\",\n";
    out << "  \"afn\": \"" << toHex({code.afn}, false) << "\",\n";
    out << "  \"data\": \"" << toHex(code.data, true) << "\",\n";
    out << "  \"frame\": \"" << toHex(code.frame, true) << "\"\n";
    out << "}\n";
}

StoredCode loadStoredCode(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("failed to open input file: " + path);
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    const std::string text = buffer.str();

    StoredCode code;
    code.name = getStringField(text, "name", false);
    code.createdAt = getStringField(text, "created_at", false);
    code.note = getStringField(text, "note", false);
    code.address = parseByteField(getStringField(text, "address", false).empty() ? "FF" : getStringField(text, "address", false),
                                  "address");
    code.afn = parseByteField(getStringField(text, "afn", false).empty() ? "22" : getStringField(text, "afn", false),
                              "afn");
    const auto data = getStringField(text, "data", false);
    if (!data.empty()) {
        code.data = fromHex(data);
    }
    const auto frame = getStringField(text, "frame", false);
    if (!frame.empty()) {
        code.frame = fromHex(frame);
    }
    if (code.data.empty() && !code.frame.empty()) {
        const auto decoded = decodeFrame(code.frame, ChecksumPolicy::StandardOrFixed55);
        code.address = decoded.address;
        code.afn = decoded.afn;
        code.data = decoded.data;
    }
    if (code.data.empty()) {
        throw std::runtime_error("stored code has no data or decodable frame");
    }
    return code;
}

}  // namespace echoir
