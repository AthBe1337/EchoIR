#include "echoir/device.hpp"

#include <array>
#include <sstream>
#include <thread>

namespace echoir {
namespace {

constexpr std::uint8_t kAfnAck = 0x01;
constexpr std::uint8_t kAfnReport = 0x02;
constexpr std::uint8_t kAfnSetBaud = 0x03;
constexpr std::uint8_t kAfnGetBaud = 0x04;
constexpr std::uint8_t kAfnSetAddress = 0x05;
constexpr std::uint8_t kAfnGetAddress = 0x06;
constexpr std::uint8_t kAfnReset = 0x07;
constexpr std::uint8_t kAfnFormat = 0x08;
constexpr std::uint8_t kAfnInternalLearn = 0x10;
constexpr std::uint8_t kAfnInternalExit = 0x11;
constexpr std::uint8_t kAfnInternalSend = 0x12;
constexpr std::uint8_t kAfnPowerOnSendSet = 0x13;
constexpr std::uint8_t kAfnPowerOnSendGet = 0x14;
constexpr std::uint8_t kAfnPowerOnDelaySet = 0x15;
constexpr std::uint8_t kAfnPowerOnDelayGet = 0x16;
constexpr std::uint8_t kAfnInternalWrite = 0x17;
constexpr std::uint8_t kAfnInternalRead = 0x18;
constexpr std::uint8_t kAfnExternalLearn = 0x20;
constexpr std::uint8_t kAfnExternalExit = 0x21;
constexpr std::uint8_t kAfnExternalCode = 0x22;
constexpr std::uint8_t kAfnAcFullControl = 0x30;
constexpr std::uint8_t kAfnAcControl = 0x31;

void requireDataSize(const Frame& frame, std::size_t size, const std::string& context) {
    if (frame.data.size() < size) {
        throw ProtocolError(context + " response is too short");
    }
}

}  // namespace

int baudRateFromIndex(std::uint8_t index) {
    static constexpr std::array<int, 5> rates = {9600, 19200, 38400, 57600, 115200};
    if (index >= rates.size()) {
        throw std::invalid_argument("baud index must be 0..4");
    }
    return rates[index];
}

std::uint8_t baudIndexFromRate(int baudRate) {
    switch (baudRate) {
        case 9600:
            return 0;
        case 19200:
            return 1;
        case 38400:
            return 2;
        case 57600:
            return 3;
        case 115200:
            return 4;
        default:
            throw std::invalid_argument("baud rate must be one of 9600, 19200, 38400, 57600, 115200");
    }
}

IrDevice::IrDevice(std::unique_ptr<ITransport> transport, std::uint8_t address, ModuleProfile profile)
    : transport_(std::move(transport)), address_(address), profile_(profile) {
    if (!transport_) {
        throw std::invalid_argument("transport must not be null");
    }
}

std::uint8_t IrDevice::maxInternalSlot() const {
    return profile_ == ModuleProfile::Basic ? 6 : 95;
}

Frame IrDevice::transact(std::uint8_t afn,
                         const Bytes& data,
                         std::chrono::milliseconds timeout,
                         ChecksumMode checksumMode,
                         ChecksumPolicy responsePolicy) {
    transport_->writeAll(encodeFrame(address_, afn, data, checksumMode));
    return readFrame(timeout, responsePolicy);
}

Ack IrDevice::commandAck(std::uint8_t afn, const Bytes& data, std::chrono::milliseconds timeout) {
    const auto frame = transact(afn, data, timeout);
    if (frame.afn != kAfnAck) {
        throw ProtocolError("expected ACK frame");
    }
    requireDataSize(frame, 1, "ACK");
    return Ack{frame.data[0]};
}

std::uint8_t IrDevice::getBaudIndex() {
    const auto frame = transact(kAfnGetBaud);
    if (frame.afn != kAfnGetBaud) {
        throw ProtocolError("expected get-baud response");
    }
    requireDataSize(frame, 1, "get-baud");
    return frame.data[0];
}

Ack IrDevice::setBaudIndex(std::uint8_t index) {
    if (index > 4) {
        throw std::invalid_argument("baud index must be 0..4");
    }
    return commandAck(kAfnSetBaud, {index});
}

std::uint8_t IrDevice::getModuleAddress() {
    const auto frame = transact(kAfnGetAddress);
    if (frame.afn != kAfnGetAddress) {
        throw ProtocolError("expected get-address response");
    }
    requireDataSize(frame, 1, "get-address");
    return frame.data[0];
}

Ack IrDevice::setModuleAddress(std::uint8_t newAddress) {
    if (newAddress == 0xFF) {
        throw std::invalid_argument("0xFF is reserved as broadcast address");
    }
    return commandAck(kAfnSetAddress, {newAddress});
}

Ack IrDevice::reset() {
    return commandAck(kAfnReset);
}

Ack IrDevice::format() {
    return commandAck(kAfnFormat);
}

Ack IrDevice::startInternalLearn(std::uint8_t slot) {
    validateSlot(slot);
    return commandAck(kAfnInternalLearn, {slot});
}

Ack IrDevice::exitInternalLearn() {
    return commandAck(kAfnInternalExit);
}

LearnReport IrDevice::waitInternalLearnReport(std::chrono::milliseconds timeout) {
    const auto frame = readFrame(timeout);
    if (frame.afn != kAfnReport) {
        throw ProtocolError("expected internal learn report frame");
    }
    requireDataSize(frame, 3, "internal learn report");
    return LearnReport{frame.data[0], frame.data[1], frame.data[2]};
}

LearnReport IrDevice::learnInternal(std::uint8_t slot, std::chrono::milliseconds timeout) {
    const auto ack = startInternalLearn(slot);
    if (!ack.ok()) {
        throw ProtocolError("module rejected internal learn command");
    }
    return waitInternalLearnReport(timeout);
}

Ack IrDevice::sendInternal(std::uint8_t slot) {
    validateSlot(slot);
    return commandAck(kAfnInternalSend, {slot});
}

Ack IrDevice::setPowerOnSend(std::uint8_t slot, bool enabled) {
    validateSlot(slot);
    return commandAck(kAfnPowerOnSendSet, {slot, static_cast<std::uint8_t>(enabled ? 1 : 0)});
}

bool IrDevice::getPowerOnSend(std::uint8_t slot) {
    validateSlot(slot);
    const auto frame = transact(kAfnPowerOnSendGet, {slot});
    if (frame.afn != kAfnPowerOnSendGet) {
        throw ProtocolError("expected get-power-on-send response");
    }
    requireDataSize(frame, 2, "get-power-on-send");
    return frame.data[1] != 0;
}

Ack IrDevice::setPowerOnDelaySeconds(std::uint16_t seconds) {
    return commandAck(kAfnPowerOnDelaySet,
                      {static_cast<std::uint8_t>(seconds & 0xFFU),
                       static_cast<std::uint8_t>((seconds >> 8U) & 0xFFU)});
}

std::uint16_t IrDevice::getPowerOnDelaySeconds() {
    const auto frame = transact(kAfnPowerOnDelayGet);
    if (frame.afn != kAfnPowerOnDelayGet) {
        throw ProtocolError("expected get-power-on-delay response");
    }
    requireDataSize(frame, 2, "get-power-on-delay");
    return static_cast<std::uint16_t>(frame.data[0] | (static_cast<std::uint16_t>(frame.data[1]) << 8U));
}

Ack IrDevice::writeInternal(std::uint8_t slot, const Bytes& code) {
    validateSlot(slot);
    Bytes data;
    data.reserve(code.size() + 1);
    data.push_back(slot);
    data.insert(data.end(), code.begin(), code.end());
    return commandAck(kAfnInternalWrite, data);
}

InternalCode IrDevice::readInternal(std::uint8_t slot) {
    validateSlot(slot);
    const auto frame = transact(kAfnInternalRead, {slot}, std::chrono::milliseconds(1500));
    if (frame.afn != kAfnInternalRead) {
        throw ProtocolError("expected read-internal response");
    }
    requireDataSize(frame, 2, "read-internal");
    InternalCode code;
    code.slot = frame.data[0];
    code.status = frame.data[1];
    if (frame.data.size() > 2) {
        code.code.assign(frame.data.begin() + 2, frame.data.end());
    }
    return code;
}

Ack IrDevice::startExternalLearn() {
    return commandAck(kAfnExternalLearn);
}

Ack IrDevice::exitExternalLearn() {
    return commandAck(kAfnExternalExit);
}

Bytes IrDevice::waitExternalCode(std::chrono::milliseconds timeout) {
    const auto frame = readFrame(timeout);
    if (frame.afn != kAfnExternalCode) {
        throw ProtocolError("expected external code frame");
    }
    return frame.data;
}

Bytes IrDevice::learnExternal(std::chrono::milliseconds timeout) {
    const auto ack = startExternalLearn();
    if (!ack.ok()) {
        throw ProtocolError("module rejected external learn command");
    }
    return waitExternalCode(timeout);
}

Ack IrDevice::sendExternal(const Bytes& code) {
    return commandAck(kAfnExternalCode, code, std::chrono::milliseconds(1500));
}

void IrDevice::sendAcParameter(std::uint8_t parameterIndex,
                               std::uint8_t value,
                               bool waitForAck,
                               std::chrono::milliseconds timeout) {
    transport_->writeAll(encodeFrame(address_, kAfnAcControl, {parameterIndex, value}, ChecksumMode::Fixed55));
    if (waitForAck) {
        const auto frame = readFrame(timeout, ChecksumPolicy::StandardOrFixed55);
        if (frame.afn != kAfnAck) {
            throw ProtocolError("expected AC ACK frame");
        }
        requireDataSize(frame, 1, "AC ACK");
        if (frame.data[0] != 0) {
            throw ProtocolError("module rejected AC command");
        }
    }
}

void IrDevice::sendAcFull(std::uint8_t brand,
                          std::uint8_t power,
                          std::uint8_t mode,
                          std::uint8_t temperature,
                          std::uint8_t fan,
                          bool waitForAck,
                          std::chrono::milliseconds timeout) {
    transport_->writeAll(encodeFrame(address_,
                                     kAfnAcFullControl,
                                     {brand, power, mode, temperature, fan},
                                     ChecksumMode::Fixed55));
    if (waitForAck) {
        const auto frame = readFrame(timeout, ChecksumPolicy::StandardOrFixed55);
        if (frame.afn != kAfnAck) {
            throw ProtocolError("expected AC ACK frame");
        }
        requireDataSize(frame, 1, "AC ACK");
        if (frame.data[0] != 0) {
            throw ProtocolError("module rejected AC command");
        }
    }
}

Frame IrDevice::readFrame(std::chrono::milliseconds timeout, ChecksumPolicy policy) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    Frame frame;
    while (std::chrono::steady_clock::now() < deadline) {
        if (tryPopFrame(rxBuffer_, frame, policy)) {
            return frame;
        }
        const auto now = std::chrono::steady_clock::now();
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        const auto chunkTimeout = remaining > std::chrono::milliseconds(50) ? std::chrono::milliseconds(50) : remaining;
        if (chunkTimeout.count() <= 0) {
            break;
        }
        auto bytes = transport_->readSome(1024, chunkTimeout);
        rxBuffer_.insert(rxBuffer_.end(), bytes.begin(), bytes.end());
    }
    throw ProtocolError("timed out waiting for frame");
}

void IrDevice::validateSlot(std::uint8_t slot) const {
    if (slot > maxInternalSlot()) {
        std::ostringstream message;
        message << "slot must be 0.." << static_cast<unsigned int>(maxInternalSlot());
        throw std::invalid_argument(message.str());
    }
}

}  // namespace echoir
