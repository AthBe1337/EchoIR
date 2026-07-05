#include "echoir/ac.hpp"
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
    const auto daikin2 = echoir::resolveAcBrand("daikin2");
    require(daikin2 && *daikin2 == 0x91, "builtin daikin2 brand code");
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
