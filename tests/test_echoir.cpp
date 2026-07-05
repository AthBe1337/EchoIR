#include "echoir/ac.hpp"
#include "echoir/ac_official.hpp"
#include "echoir/code_store.hpp"
#include "echoir/device.hpp"
#include "echoir/frame.hpp"
#include "echoir/serial.hpp"

#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using echoir::Bytes;

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void requireEq(const Bytes& actual, const Bytes& expected, const std::string& message) {
    if (actual != expected) {
        throw std::runtime_error(message + "\nactual:   " + echoir::toHex(actual) +
                                 "\nexpected: " + echoir::toHex(expected));
    }
}

class FakeTransport final : public echoir::ITransport {
public:
    void writeAll(const Bytes& bytes) override {
        writes.push_back(bytes);
    }

    Bytes readSome(std::size_t, std::chrono::milliseconds) override {
        if (reads.empty()) {
            return {};
        }
        auto out = reads.front();
        reads.erase(reads.begin());
        return out;
    }

    std::vector<Bytes> writes;
    std::vector<Bytes> reads;
};

std::unique_ptr<FakeTransport> fakeWithRead(const Bytes& frame) {
    auto fake = std::make_unique<FakeTransport>();
    fake->reads.push_back(frame);
    return fake;
}

void testFrameEncoding() {
    requireEq(echoir::encodeFrame(0xFF, 0x04, {}),
              echoir::fromHex("68 07 00 FF 04 03 16"),
              "get-baud frame must match manual");
    requireEq(echoir::encodeFrame(0xFF, 0x10, {0x00}),
              echoir::fromHex("68 08 00 FF 10 00 0F 16"),
              "internal learn slot 0 frame must match manual");
    requireEq(echoir::encodeFrame(0xFF, 0x12, {0x06}),
              echoir::fromHex("68 08 00 FF 12 06 17 16"),
              "internal send slot 6 frame must match manual");

    const auto frame = echoir::decodeFrame(echoir::fromHex("68 08 00 00 04 04 08 16"));
    require(frame.address == 0x00, "decoded address");
    require(frame.afn == 0x04, "decoded afn");
    requireEq(frame.data, {0x04}, "decoded data");
}

void testFrameResync() {
    Bytes buffer = echoir::fromHex("00 FF 68 07 00 FF 06 05 16");
    echoir::Frame frame;
    require(echoir::tryPopFrame(buffer, frame), "tryPopFrame should resync after noise");
    require(frame.afn == 0x06, "resynced frame afn");
    require(buffer.empty(), "buffer consumed");
}

void testAcEncoding() {
    requireEq(echoir::encodeAcFrame(echoir::AcParameter::Brand, 0x10),
              echoir::fromHex("68 09 00 FF 31 00 10 55 16"),
              "AC brand midea frame");
    requireEq(echoir::encodeAcFrame(echoir::AcParameter::Power, echoir::encodeAcPower(echoir::AcPower::On)),
              echoir::fromHex("68 09 00 FF 31 01 01 55 16"),
              "AC power on frame");
    requireEq(echoir::encodeAcFrame(echoir::AcParameter::Mode, echoir::encodeAcMode(echoir::AcMode::Cool)),
              echoir::fromHex("68 09 00 FF 31 02 01 55 16"),
              "AC cool mode frame");
    requireEq(echoir::encodeAcFrame(echoir::AcParameter::Temperature, echoir::encodeAcTemperature(26)),
              echoir::fromHex("68 09 00 FF 31 03 1A 55 16"),
              "AC temp 26 frame");
    requireEq(echoir::encodeAcFrame(echoir::AcParameter::Fan, echoir::encodeAcFan(echoir::AcFan::Medium)),
              echoir::fromHex("68 09 00 FF 31 04 02 55 16"),
              "AC medium fan frame");
    requireEq(echoir::encodeAcFullFrame({0x40,
                                         echoir::encodeAcPower(echoir::AcPower::On),
                                         echoir::encodeAcMode(echoir::AcMode::Cool),
                                         echoir::encodeAcTemperature(24),
                                         echoir::encodeAcFan(echoir::AcFan::High)}),
              echoir::fromHex("68 0C 00 FF 30 40 01 01 18 03 55 16"),
              "AC full command frame");

    const auto brand = echoir::resolveAcBrand("midea");
    require(brand && *brand == 0x10, "builtin midea brand code");
    const auto gree = echoir::resolveAcBrand("gree");
    require(gree && *gree == 0x00, "builtin gree brand code");
    const auto haier1 = echoir::resolveAcBrand("haier1");
    require(haier1 && *haier1 == 0x20, "builtin haier1 brand code");
    const auto haier2 = echoir::resolveAcBrand("haier2");
    require(haier2 && *haier2 == 0x21, "builtin haier2 brand code");
    const auto haier3 = echoir::resolveAcBrand("haier3");
    require(haier3 && *haier3 == 0x22, "builtin haier3 brand code");
    const auto haier4 = echoir::resolveAcBrand("haier4");
    require(haier4 && *haier4 == 0x23, "builtin haier4 brand code");
    const auto chiq1 = echoir::resolveAcBrand("chiq1");
    require(chiq1 && *chiq1 == 0x80, "builtin chiq1 brand code");
    const auto daikin2 = echoir::resolveAcBrand("daikin2");
    require(daikin2 && *daikin2 == 0x91, "builtin daikin2 brand code");
}

void testOfficialMidea1Encoding() {
    echoir::OfficialMidea1Options options;
    options.power = true;
    options.mode = echoir::AcMode::Cool;
    options.temperature = 24;
    options.fan = echoir::AcFan::Auto;

    const auto durations = echoir::generateMidea1Durations(options);
    require(durations.size() == 299, "Midea1 power-on duration count");
    require(durations[0] == 4400 && durations[1] == 4400, "Midea1 leader");
    require(durations[198] == 540 && durations[199] == 5220, "Midea1 third packet gap");
    require(durations[200] == 4400 && durations[201] == 4400, "Midea1 third packet leader");
    require(durations.back() == 540, "Midea1 trailing mark");

    const auto code = echoir::encodeOfficialMidea1(options);
    require(code.size() > durations.size(), "Midea1 compressed code uses varints for long durations");
    requireEq(Bytes(code.begin(), code.begin() + 4), {0xA6, 0x04, 0xA6, 0x04}, "Midea1 compressed leader");

    const auto frame = echoir::encodeFrame(0xFF, 0x22, code);
    const auto decoded = echoir::decodeFrame(frame);
    require(decoded.afn == 0x22, "Midea1 official frame uses external-code AFN");
    requireEq(decoded.data, code, "Midea1 official frame payload");
}

void testOfficialMidea2Encoding() {
    echoir::OfficialMidea2Options options;
    options.power = true;
    options.mode = echoir::AcMode::Cool;
    options.temperature = 24;
    options.fan = echoir::AcFan::Auto;

    const auto durations = echoir::generateMidea2Durations(options);
    require(durations.size() == 199, "Midea2 duration count");
    require(durations[0] == 4400 && durations[1] == 4400, "Midea2 leader");
    require(durations.back() == 540, "Midea2 trailing mark");

    const auto code = echoir::encodeOfficialMidea2(options);
    requireEq(Bytes(code.begin(), code.begin() + 4), {0xA6, 0x04, 0xA6, 0x04}, "Midea2 compressed leader");

    const auto frame = echoir::encodeFrame(0xFF, 0x22, code);
    const auto decoded = echoir::decodeFrame(frame);
    require(decoded.afn == 0x22, "Midea2 official frame uses external-code AFN");
    requireEq(decoded.data, code, "Midea2 official frame payload");
}

void testOfficialAllProtocolEncoders() {
    const auto& protocols = echoir::officialAcProtocols();
    require(protocols.size() == 21, "official AC protocol count");

    for (const auto protocol : protocols) {
        const std::string name = echoir::officialAcProtocolName(protocol);
        const auto parsed = echoir::parseOfficialAcProtocol(name);
        require(parsed && *parsed == protocol, "official AC protocol parser: " + name);

        const auto state = echoir::defaultOfficialAcState(protocol);
        const auto durations = echoir::generateOfficialAcDurations(state);
        require(!durations.empty(), "official AC durations empty: " + name);
        for (const auto duration : durations) {
            require(duration > 0, "official AC duration must be positive: " + name);
        }

        const auto code = echoir::encodeOfficialAc(state);
        require(!code.empty(), "official AC compressed code empty: " + name);
        require(code.size() <= 0x310, "official AC compressed code exceeds module payload limit: " + name);

        const auto frame = echoir::encodeFrame(0xFF, 0x22, code);
        const auto decoded = echoir::decodeFrame(frame);
        require(decoded.afn == 0x22, "official AC frame AFN: " + name);
        requireEq(decoded.data, code, "official AC frame payload: " + name);
    }
}

void testDeviceAckCommand() {
    auto fake = fakeWithRead(echoir::fromHex("68 08 00 00 01 00 01 16"));
    auto* raw = fake.get();
    echoir::IrDevice device(std::move(fake));
    const auto ack = device.sendInternal(0);
    require(ack.ok(), "sendInternal ACK");
    require(raw->writes.size() == 1, "one write");
    requireEq(raw->writes.front(), echoir::fromHex("68 08 00 FF 12 00 11 16"), "sendInternal write bytes");
}

void testDeviceReadInternal() {
    auto fake = fakeWithRead(echoir::fromHex("68 0B 00 00 18 00 00 AA BB 7D 16"));
    echoir::IrDevice device(std::move(fake));
    const auto code = device.readInternal(0);
    require(code.ok(), "readInternal status");
    require(code.slot == 0, "readInternal slot");
    requireEq(code.code, {0xAA, 0xBB}, "readInternal code data");
}

void testCodeStore() {
    const std::string path = "/tmp/echoir_code_store_test.json";
    echoir::StoredCode saved;
    saved.name = "sample";
    saved.createdAt = "2026-07-05T00:00:00+0000";
    saved.address = 0xFF;
    saved.afn = 0x22;
    saved.data = {0x84, 0x01, 0x21};
    saved.frame = echoir::encodeFrame(saved.address, saved.afn, saved.data);
    echoir::saveStoredCode(path, saved);

    const auto loaded = echoir::loadStoredCode(path);
    require(loaded.name == saved.name, "stored name");
    require(loaded.address == saved.address, "stored address");
    require(loaded.afn == saved.afn, "stored afn");
    requireEq(loaded.data, saved.data, "stored data");
    requireEq(loaded.frame, saved.frame, "stored frame");
}

}  // namespace

int main() {
    const std::vector<std::pair<std::string, void (*)()>> tests = {
        {"frame_encoding", testFrameEncoding},
        {"frame_resync", testFrameResync},
        {"ac_encoding", testAcEncoding},
        {"official_midea1_encoding", testOfficialMidea1Encoding},
        {"official_midea2_encoding", testOfficialMidea2Encoding},
        {"official_all_protocol_encoders", testOfficialAllProtocolEncoders},
        {"device_ack_command", testDeviceAckCommand},
        {"device_read_internal", testDeviceReadInternal},
        {"code_store", testCodeStore},
    };

    int failures = 0;
    for (const auto& test : tests) {
        try {
            test.second();
            std::cout << "[PASS] " << test.first << "\n";
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "[FAIL] " << test.first << ": " << error.what() << "\n";
        }
    }
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
