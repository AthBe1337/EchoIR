#pragma once

#include "echoir/frame.hpp"

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>

namespace echoir {

class ITransport {
public:
    virtual ~ITransport() = default;
    virtual void writeAll(const Bytes& bytes) = 0;
    virtual Bytes readSome(std::size_t maxBytes, std::chrono::milliseconds timeout) = 0;
};

class SerialPort final : public ITransport {
public:
    SerialPort(const std::string& path, int baudRate);
    ~SerialPort() override;

    SerialPort(const SerialPort&) = delete;
    SerialPort& operator=(const SerialPort&) = delete;

    void writeAll(const Bytes& bytes) override;
    Bytes readSome(std::size_t maxBytes, std::chrono::milliseconds timeout) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::unique_ptr<ITransport> openSerialTransport(const std::string& path, int baudRate);

}  // namespace echoir
