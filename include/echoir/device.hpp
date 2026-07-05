#pragma once

#include "echoir/frame.hpp"
#include "echoir/serial.hpp"

#include <chrono>
#include <memory>

namespace echoir {

enum class ModuleProfile {
    Basic,
    High,
};

struct Ack {
    std::uint8_t status = 0;
    bool ok() const { return status == 0; }
};

struct LearnReport {
    std::uint8_t flag = 0;
    std::uint8_t slot = 0;
    std::uint8_t status = 0;
    bool ok() const { return status == 0; }
};

struct InternalCode {
    std::uint8_t slot = 0;
    std::uint8_t status = 0;
    Bytes code;
    bool ok() const { return status == 0; }
};

class IrDevice {
public:
    explicit IrDevice(std::unique_ptr<ITransport> transport,
                      std::uint8_t address = 0xFF,
                      ModuleProfile profile = ModuleProfile::High);

    std::uint8_t address() const { return address_; }
    std::uint8_t maxInternalSlot() const;

    Frame transact(std::uint8_t afn,
                   const Bytes& data = {},
                   std::chrono::milliseconds timeout = std::chrono::milliseconds(1000),
                   ChecksumMode checksumMode = ChecksumMode::StandardSum,
                   ChecksumPolicy responsePolicy = ChecksumPolicy::StandardOnly);

    Ack commandAck(std::uint8_t afn,
                   const Bytes& data = {},
                   std::chrono::milliseconds timeout = std::chrono::milliseconds(1000));

    std::uint8_t getBaudIndex();
    Ack setBaudIndex(std::uint8_t index);
    std::uint8_t getModuleAddress();
    Ack setModuleAddress(std::uint8_t newAddress);
    Ack reset();
    Ack format();

    Ack startInternalLearn(std::uint8_t slot);
    Ack exitInternalLearn();
    LearnReport waitInternalLearnReport(std::chrono::milliseconds timeout);
    LearnReport learnInternal(std::uint8_t slot, std::chrono::milliseconds timeout);
    Ack sendInternal(std::uint8_t slot);
    Ack setPowerOnSend(std::uint8_t slot, bool enabled);
    bool getPowerOnSend(std::uint8_t slot);
    Ack setPowerOnDelaySeconds(std::uint16_t seconds);
    std::uint16_t getPowerOnDelaySeconds();
    Ack writeInternal(std::uint8_t slot, const Bytes& code);
    InternalCode readInternal(std::uint8_t slot);

    Ack startExternalLearn();
    Ack exitExternalLearn();
    Bytes waitExternalCode(std::chrono::milliseconds timeout);
    Bytes learnExternal(std::chrono::milliseconds timeout);
    Ack sendExternal(const Bytes& code);

    void sendAcParameter(std::uint8_t parameterIndex,
                         std::uint8_t value,
                         bool waitForAck = false,
                         std::chrono::milliseconds timeout = std::chrono::milliseconds(300));
    void sendAcFull(std::uint8_t brand,
                    std::uint8_t power,
                    std::uint8_t mode,
                    std::uint8_t temperature,
                    std::uint8_t fan,
                    bool waitForAck = false,
                    std::chrono::milliseconds timeout = std::chrono::milliseconds(300));

private:
    Frame readFrame(std::chrono::milliseconds timeout,
                    ChecksumPolicy policy = ChecksumPolicy::StandardOnly);
    void validateSlot(std::uint8_t slot) const;

    std::unique_ptr<ITransport> transport_;
    std::uint8_t address_ = 0xFF;
    ModuleProfile profile_ = ModuleProfile::High;
    Bytes rxBuffer_;
};

int baudRateFromIndex(std::uint8_t index);
std::uint8_t baudIndexFromRate(int baudRate);

}  // namespace echoir
